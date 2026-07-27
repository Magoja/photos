#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ui {

// Modal confirmation for destructive catalog deletes. Holds a pending target and
// fires onConfirm(target) only when the user clicks Delete. The caller dispatches
// the matching command and refreshes the UI.
class DeleteConfirmDialog {
 public:
  enum class Kind {
    Photos,
    Folder,
  };

  struct Target {
    Kind kind = Kind::Photos;
    std::vector<int64_t> ids;  // photos to delete (Kind::Photos)
    int64_t folderId = 0;      // folder to delete (Kind::Folder)
    std::string message;       // human-readable confirmation prompt
  };

  using ConfirmCb = std::function<void(const Target&)>;

  void setOnConfirm(ConfirmCb cb) { onConfirm_ = std::move(cb); }

  void openForPhotos(std::vector<int64_t> ids);
  void openForFolder(int64_t folderId, const std::string& name);
  void close();
  bool isOpen() const { return open_; }

  void render();

 private:
  bool open_ = false;
  Target target_;
  ConfirmCb onConfirm_;
};

}  // namespace ui
