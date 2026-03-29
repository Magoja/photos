#include "ImportDialog.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "util/Platform.h"
#include <filesystem>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace fs = std::filesystem;

namespace ui {

// ── Construction / destruction ────────────────────────────────────────────────

ImportDialog::ImportDialog(catalog::Database& db, TextureManager& texMgr)
    : db_(db), texMgr_(texMgr) {}

ImportDialog::~ImportDialog() {
  cancelAll();
  // unique_ptr destructors join background threads after cancelAll() unblocks them
}

// ── Public API ────────────────────────────────────────────────────────────────

void ImportDialog::open(const std::string& sourcePath, const std::string& destPath,
                        const std::string& thumbCacheRoot) {
  cancelAll();
  scanner_.reset();
  importer_.reset();

  sourcePath_ = sourcePath;
  destPath_   = destPath;
  thumbRoot_  = thumbCacheRoot;
  copyFiles_  = true;
  open_       = true;
  state_      = State::kIdle;

  previewItems_.clear();
  {
    std::lock_guard lk(pendingMtx_);
    pendingItems_.clear();
  }
  selectedIndices_.clear();
  shiftAnchor_   = -1;
  nextPreviewId_ = -1;
  scanDone_      = 0;
  scanTotal_     = 0;
  importDone_    = false;

  totalFiles_ = 0;
  doneFiles_  = 0;
  {
    std::lock_guard lk(progressMtx_);
    currentFile_.clear();
  }

  {
    std::lock_guard lk(conflictMtx_);
    conflictResolved_ = true;
  }
  conflictPending_ = false;

  // Auto-uncheck copy if source is already inside dest
  if (!sourcePath_.empty()) {
    std::error_code ec;
    const auto rel = fs::relative(sourcePath_, destPath_, ec);
    if (!ec && !rel.empty() && rel.native().find("..") == std::string::npos) {
      copyFiles_ = false;
    }
  }
}

void ImportDialog::close() {
  cancelAll();
  scanner_.reset();
  importer_.reset();
  open_  = false;
  state_ = State::kIdle;
}

// ── cancelAll: unblocks background threads so their destructors can join ──────

void ImportDialog::cancelAll() {
  if (scanner_)  { scanner_->cancel(); }
  if (importer_) { importer_->cancel(); }
  {
    std::lock_guard lk(conflictMtx_);
    conflictResolution_ = import_ns::ConflictResolution::Skip;
    conflictResolved_   = true;
  }
  conflictCv_.notify_all();
}

// ── Scan phase ────────────────────────────────────────────────────────────────

void ImportDialog::startScan() {
  previewItems_.clear();
  {
    std::lock_guard lk(pendingMtx_);
    pendingItems_.clear();
  }
  selectedIndices_.clear();
  shiftAnchor_   = -1;
  nextPreviewId_ = -1;
  scanDone_      = 0;
  scanTotal_     = 0;

  scanner_ = std::make_unique<import_ns::PreviewScanner>(db_);
  scanner_->start(
      sourcePath_,
      [this](int done, int total) {
        scanDone_  = done;
        scanTotal_ = total;
      },
      [this](import_ns::PreviewItem item) {
        std::lock_guard lk(pendingMtx_);
        pendingItems_.push_back(std::move(item));
      });
  state_ = State::kScanning;
}

void ImportDialog::drainScanQueue() {
  std::vector<import_ns::PreviewItem> batch;
  {
    std::lock_guard lk(pendingMtx_);
    batch = std::move(pendingItems_);
    pendingItems_.clear();
  }
  for (auto& item : batch) {
    item.previewId = nextPreviewId_--;
    if (!item.thumbJpeg.empty()) {
      if (texMgr_.upload(item.previewId, item.thumbJpeg)) {
        item.texture = texMgr_.get(item.previewId);
      }
    }
    selectedIndices_.insert(static_cast<int>(previewItems_.size()));
    previewItems_.push_back(std::move(item));
  }
}

// ── Import phase ──────────────────────────────────────────────────────────────

void ImportDialog::startImport() {
  std::vector<std::string> selectedPaths;
  selectedPaths.reserve(selectedIndices_.size());
  for (const int idx : selectedIndices_) {
    if (idx >= 0 && idx < static_cast<int>(previewItems_.size())) {
      selectedPaths.push_back(previewItems_[idx].path);
    }
  }

  import_ns::ImportOptions opts;
  opts.sourcePath     = sourcePath_;
  opts.destPath       = destPath_;
  opts.thumbCacheRoot = thumbRoot_;
  opts.copyFiles      = copyFiles_;
  opts.selectedFiles  = std::move(selectedPaths);
  opts.conflictCb     = [this](const std::string& fn, const std::string& dir) {
    return handleConflict(fn, dir);
  };

  totalFiles_ = 0;
  doneFiles_  = 0;
  importDone_ = false;
  {
    std::lock_guard lk(progressMtx_);
    currentFile_.clear();
  }

  importer_ = std::make_unique<import_ns::Importer>(db_, opts);
  importer_->setProgressCallback([this](int done, int total, const std::string& file) {
    doneFiles_  = done;
    totalFiles_ = total;
    std::lock_guard lk(progressMtx_);
    currentFile_ = fs::path(file).filename().string();
  });
  importer_->setDoneCallback([this](const import_ns::ImportStats& s) {
    {
      std::lock_guard lk(progressMtx_);
      stats_ = s;
    }
    importDone_ = true;
  });
  importer_->start();
  state_ = State::kImporting;
}

// ── Conflict handling ─────────────────────────────────────────────────────────

import_ns::ConflictResolution ImportDialog::handleConflict(const std::string& filename,
                                                           const std::string& destDir) {
  {
    std::lock_guard lk(conflictMtx_);
    conflictInfo_     = {filename, destDir};
    conflictResolved_ = false;
  }
  conflictPending_ = true;

  std::unique_lock lk(conflictMtx_);
  conflictCv_.wait(lk, [this] { return conflictResolved_; });
  return conflictResolution_;
}

void ImportDialog::resolveConflict(const import_ns::ConflictResolution res) {
  {
    std::lock_guard lk(conflictMtx_);
    conflictResolution_ = res;
    conflictResolved_   = true;
  }
  conflictPending_ = false;
  conflictCv_.notify_all();
}

// ── Render entry point ────────────────────────────────────────────────────────

void ImportDialog::render() {
  if (!open_) { return; }

  ImGui::SetNextWindowSize({900, 680}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver,
                          {0.5f, 0.5f});
  if (!ImGui::Begin("Import Photos##dlg", &open_)) {
    ImGui::End();
    return;
  }

  // Detect window closed via X button
  if (!open_) {
    cancelAll();
    scanner_.reset();
    importer_.reset();
    state_ = State::kIdle;
    ImGui::End();
    return;
  }

  switch (state_) {
    case State::kIdle:      renderIdle();      break;
    case State::kScanning:  renderScanning();  break;
    case State::kPreview:   renderPreview();   break;
    case State::kImporting: renderImporting(); break;
    case State::kDone:      renderDone();      break;
  }

  ImGui::End();
}

// ── State renderers ───────────────────────────────────────────────────────────

void ImportDialog::renderIdle() {
  ImGui::Text("Source:");
  ImGui::SameLine(100);
  ImGui::SetNextItemWidth(680);
  ImGui::InputText("##src", &sourcePath_);
  ImGui::SameLine();
  if (ImGui::Button("Browse##src")) {
    if (const auto picked = util::pickFolder()) {
      sourcePath_ = *picked;
      std::error_code ec;
      const auto rel = fs::relative(sourcePath_, destPath_, ec);
      copyFiles_ = ec || rel.empty() || rel.native().find("..") != std::string::npos;
    }
  }

  ImGui::Text("Destination:");
  ImGui::SameLine(100);
  ImGui::TextDisabled("%s", destPath_.c_str());

  ImGui::Checkbox("Copy files to library", &copyFiles_);
  ImGui::Separator();

  const bool canScan = !sourcePath_.empty();
  if (!canScan) { ImGui::BeginDisabled(); }
  if (ImGui::Button("Scan for Photos")) { startScan(); }
  if (!canScan) { ImGui::EndDisabled(); }

  ImGui::SameLine();
  if (ImGui::Button("Cancel##idle")) { close(); }
}

void ImportDialog::renderScanning() {
  drainScanQueue();

  const int   done     = scanDone_;
  const int   total    = scanTotal_;
  const float progress = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.f;

  ImGui::Text("Scanning: %d / %d files", done, total);
  ImGui::ProgressBar(progress, {-1, 0});
  ImGui::Text("%d photos found so far (duplicates excluded)", static_cast<int>(previewItems_.size()));

  if (ImGui::Button("Cancel##scan")) {
    cancelAll();
    scanner_.reset();
    state_ = State::kIdle;
    return;
  }

  // Transition once scanner thread finishes and queue is fully drained
  if (!scanner_->isRunning()) {
    drainScanQueue();
    selectedIndices_.clear();
    for (int i = 0; i < static_cast<int>(previewItems_.size()); ++i) {
      selectedIndices_.insert(i);
    }
    state_ = State::kPreview;
  }
}

void ImportDialog::renderPreview() {
  drainScanQueue();

  const int found    = static_cast<int>(previewItems_.size());
  const int total    = scanTotal_;
  const int dupCount = total - found;

  ImGui::Text("Source: %s", sourcePath_.c_str());
  ImGui::Text("%d photos  (%d duplicates excluded)", found, dupCount);
  ImGui::Separator();

  constexpr float kCellSize = 120.0f;
  constexpr float kCellPad  = 4.0f;
  const float     availW    = ImGui::GetContentRegionAvail().x;
  const int       cols      = std::max(1, static_cast<int>(availW / (kCellSize + kCellPad)));

  const float bottomBarH = 45.0f;
  ImGui::BeginChild("##previewGrid", {0, -bottomBarH}, false, 0);

  const auto& io = ImGui::GetIO();

  for (int i = 0; i < found; ++i) {
    const auto& item       = previewItems_[i];
    const bool  isSelected = selectedIndices_.count(i) > 0;

    ImGui::PushID(i);

    const void* tex = item.texture ? item.texture : texMgr_.placeholder();
    ImGui::Image(reinterpret_cast<ImTextureID>(tex), {kCellSize, kCellSize});

    if (isSelected) {
      auto* dl        = ImGui::GetWindowDrawList();
      const auto rmin = ImGui::GetItemRectMin();
      const auto rmax = ImGui::GetItemRectMax();
      dl->AddRect(rmin, rmax, IM_COL32(100, 180, 255, 220), 0.f, 0, 2.5f);
    }

    if (ImGui::IsItemClicked()) {
      const bool cmdHeld   = (io.KeyMods & ImGuiMod_Super) != 0;
      const bool shiftHeld = io.KeyShift;

      if (shiftHeld && shiftAnchor_ >= 0) {
        const int lo = std::min(i, shiftAnchor_);
        const int hi = std::max(i, shiftAnchor_);
        selectedIndices_.clear();
        for (int j = lo; j <= hi; ++j) { selectedIndices_.insert(j); }
      } else if (cmdHeld) {
        if (isSelected) { selectedIndices_.erase(i); }
        else            { selectedIndices_.insert(i); }
        shiftAnchor_ = i;
      } else {
        selectedIndices_.clear();
        selectedIndices_.insert(i);
        shiftAnchor_ = i;
      }
    }

    ImGui::TextUnformatted(item.filename.c_str());
    ImGui::PopID();

    if ((i + 1) % cols != 0) {
      ImGui::SameLine(0.f, kCellPad);
    }
  }

  ImGui::EndChild();
  ImGui::Separator();

  ImGui::Text("%d selected", static_cast<int>(selectedIndices_.size()));
  ImGui::SameLine();

  const bool canImport = !selectedIndices_.empty();
  if (!canImport) { ImGui::BeginDisabled(); }
  if (ImGui::Button("Import Selected")) { startImport(); }
  if (!canImport) { ImGui::EndDisabled(); }

  ImGui::SameLine();
  if (ImGui::Button("Select All")) {
    selectedIndices_.clear();
    for (int i = 0; i < found; ++i) { selectedIndices_.insert(i); }
  }
  ImGui::SameLine();
  if (ImGui::Button("Deselect All")) { selectedIndices_.clear(); }
  ImGui::SameLine();
  if (ImGui::Button("Cancel##preview")) { close(); }
  ImGui::SameLine();
  ImGui::Checkbox("Copy files", &copyFiles_);
}

void ImportDialog::renderImporting() {
  // Check for import completion (set by background thread)
  if (importDone_) {
    state_      = State::kDone;
    importDone_ = false;
    if (doneCb_) { doneCb_(); }
    return;
  }

  // Conflict modal overlay (blocks import thread via condition_variable)
  if (conflictPending_) {
    renderConflictModal();
  }

  const int   done     = doneFiles_;
  const int   total    = totalFiles_;
  const float progress = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.f;

  std::string curFile;
  {
    std::lock_guard lk(progressMtx_);
    curFile = currentFile_;
  }

  ImGui::Text("Importing: %s", curFile.c_str());
  ImGui::ProgressBar(progress, {-1, 0});
  ImGui::Text("%d / %d files", done, total);
  ImGui::Separator();
  if (ImGui::Button("Cancel##imp")) {
    importer_->cancel();
  }
}

void ImportDialog::renderConflictModal() {
  ImGui::OpenPopup("File Conflict##modal");
  if (!ImGui::BeginPopupModal("File Conflict##modal", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  std::string filename, destDir;
  {
    std::lock_guard lk(conflictMtx_);
    filename = conflictInfo_.filename;
    destDir  = conflictInfo_.destDir;
  }

  ImGui::Text("%s already exists in:", filename.c_str());
  ImGui::TextDisabled("%s", destDir.c_str());
  ImGui::Separator();

  if (ImGui::Button("Skip")) {
    resolveConflict(import_ns::ConflictResolution::Skip);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();

  const auto stem    = fs::path(filename).stem().string();
  const auto ext     = fs::path(filename).extension().string();
  const auto renamed = stem + "-1" + ext;
  if (ImGui::Button(("Rename to " + renamed).c_str())) {
    resolveConflict(import_ns::ConflictResolution::Rename);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();

  if (ImGui::Button("Overwrite")) {
    resolveConflict(import_ns::ConflictResolution::Overwrite);
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

void ImportDialog::renderDone() {
  import_ns::ImportStats s;
  {
    std::lock_guard lk(progressMtx_);
    s = stats_;
  }
  ImGui::TextColored({0.2f, 1.f, 0.2f, 1.f},
                     "Done!  %d new,  %d duplicates,  %d errors",
                     s.imported, s.duplicates, s.errors);
  if (ImGui::Button("Close")) { close(); }
}

}  // namespace ui
