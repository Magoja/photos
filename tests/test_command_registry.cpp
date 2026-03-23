#include <catch2/catch_test_macros.hpp>
#include "command/CommandRegistry.h"
#include "command/ICommandHandler.h"
#include "command/CommandResult.h"

using namespace command;

namespace {

// Accepts any params; echoes them back as data.
struct EchoHandler : ICommandHandler {
  nlohmann::json lastParams;

  ValidationResult validate(const nlohmann::json& /*params*/) const override {
    return valid();
  }
  CommandResult execute(nlohmann::json params) override {
    lastParams = params;
    return success(params);
  }
};

// Always fails in execute; accepts any params.
struct FailHandler : ICommandHandler {
  ValidationResult validate(const nlohmann::json& /*params*/) const override {
    return valid();
  }
  CommandResult execute(nlohmann::json /*params*/) override {
    return failure("always fails");
  }
};

// Requires params to contain "id" (integer).
struct StrictHandler : ICommandHandler {
  ValidationResult validate(const nlohmann::json& params) const override {
    if (!params.contains("id") || !params["id"].is_number_integer()) {
      return invalid("missing required integer field 'id'");
    }
    return valid();
  }
  CommandResult execute(nlohmann::json params) override {
    return success({{"id", params["id"]}});
  }
};

// Captures every log line emitted by CommandRegistry.
struct CapturingDeps {
  std::vector<std::string> lines;

  Dependencies make() {
    return {.log = [this](bool /*ok*/, std::string_view line) {
      lines.emplace_back(line);
    }};
  }
};

}  // namespace

// ── isValidCommandName ────────────────────────────────────────────────────────

TEST_CASE("isValidCommandName accepts valid names", "[naming]") {
  REQUIRE(isValidCommandName("image.adjust"));
  REQUIRE(isValidCommandName("catalog.pick"));
  REQUIRE(isValidCommandName("catalog.photo.open"));
  REQUIRE(isValidCommandName("export.photos"));
  REQUIRE(isValidCommandName("a.b"));
}

TEST_CASE("isValidCommandName rejects invalid names", "[naming]") {
  REQUIRE_FALSE(isValidCommandName(""));           // empty
  REQUIRE_FALSE(isValidCommandName("noDot"));      // no dot
  REQUIRE_FALSE(isValidCommandName(".leading"));   // leading dot
  REQUIRE_FALSE(isValidCommandName("trailing."));  // trailing dot
  REQUIRE_FALSE(isValidCommandName("two..dots"));  // consecutive dots
  REQUIRE_FALSE(isValidCommandName("Image.Adj"));  // uppercase
  REQUIRE_FALSE(isValidCommandName("img_adj.x"));  // underscore
  REQUIRE_FALSE(isValidCommandName("img adj.x"));  // space
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

TEST_CASE("Dispatch unknown command returns failure with name in error", "[registry]") {
  CommandRegistry reg;
  const auto result = reg.dispatch("no.such.command");
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().find("no.such.command") != std::string::npos);
}

TEST_CASE("Dispatch known command calls handler with params", "[registry]") {
  CommandRegistry reg;
  auto* echo = new EchoHandler{};
  reg.registerHandler("test.echo", std::unique_ptr<ICommandHandler>(echo));

  const nlohmann::json params = {{"key", "value"}};
  const auto result = reg.dispatch("test.echo", params);

  REQUIRE(result.has_value());
  REQUIRE(echo->lastParams["key"] == "value");
}

TEST_CASE("Multiple handlers are independent", "[registry]") {
  CommandRegistry reg;
  reg.registerHandler("cmd.a", std::make_unique<EchoHandler>());
  reg.registerHandler("cmd.b", std::make_unique<FailHandler>());

  REQUIRE(reg.dispatch("cmd.a").has_value());
  REQUIRE_FALSE(reg.dispatch("cmd.b").has_value());
  REQUIRE_FALSE(reg.dispatch("cmd.c").has_value());  // unregistered
}

TEST_CASE("Duplicate registration throws", "[registry]") {
  CommandRegistry reg;
  reg.registerHandler("dup.cmd", std::make_unique<EchoHandler>());
  REQUIRE_THROWS_AS(reg.registerHandler("dup.cmd", std::make_unique<EchoHandler>()),
                    std::invalid_argument);
}

TEST_CASE("Invalid command name throws", "[registry]") {
  CommandRegistry reg;
  REQUIRE_THROWS_AS(reg.registerHandler("BadName", std::make_unique<EchoHandler>()),
                    std::invalid_argument);
}

// ── validate() ────────────────────────────────────────────────────────────────

TEST_CASE("Dispatch returns failure when validate() rejects params", "[validate]") {
  CommandRegistry reg;
  reg.registerHandler("strict.cmd", std::make_unique<StrictHandler>());

  const auto result = reg.dispatch("strict.cmd", {{"name", "alice"}});  // missing "id"
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().find("id") != std::string::npos);
}

TEST_CASE("Dispatch calls execute() only when validate() passes", "[validate]") {
  CommandRegistry reg;
  reg.registerHandler("strict.cmd", std::make_unique<StrictHandler>());

  const auto result = reg.dispatch("strict.cmd", {{"id", 42}});
  REQUIRE(result.has_value());
  REQUIRE((*result)["id"] == 42);
}

TEST_CASE("Validation failure is logged as error", "[validate]") {
  CapturingDeps cap;
  CommandRegistry reg(cap.make());
  reg.registerHandler("strict.cmd", std::make_unique<StrictHandler>());

  reg.dispatch("strict.cmd", {});  // missing id

  REQUIRE(cap.lines.size() == 1);
  REQUIRE(cap.lines[0].find("[CMD]") != std::string::npos);
  REQUIRE(cap.lines[0].find("error") != std::string::npos);
}

// ── CommandResult factories ───────────────────────────────────────────────────

TEST_CASE("CommandResult factories", "[result]") {
  const auto ok = success({{"x", 1}});
  REQUIRE(ok.has_value());
  REQUIRE((*ok)["x"] == 1);

  const auto fail = failure("bad input");
  REQUIRE_FALSE(fail.has_value());
  REQUIRE(fail.error() == "bad input");
}

TEST_CASE("ValidationResult factories", "[result]") {
  const auto ok = valid();
  REQUIRE(ok.has_value());

  const auto fail = invalid("missing field");
  REQUIRE_FALSE(fail.has_value());
  REQUIRE(fail.error() == "missing field");
}

// ── Log injection ─────────────────────────────────────────────────────────────

TEST_CASE("Log functor receives [CMD] line on success", "[registry]") {
  CapturingDeps cap;
  CommandRegistry reg(cap.make());
  reg.registerHandler("ping.pong", std::make_unique<EchoHandler>());

  reg.dispatch("ping.pong");

  REQUIRE(cap.lines.size() == 1);
  REQUIRE(cap.lines[0].find("[CMD]") != std::string::npos);
  REQUIRE(cap.lines[0].find("ping.pong") != std::string::npos);
  REQUIRE(cap.lines[0].find("ok") != std::string::npos);
}

TEST_CASE("Log functor receives [CMD] line on failure", "[registry]") {
  CapturingDeps cap;
  CommandRegistry reg(cap.make());
  reg.registerHandler("cmd.boom", std::make_unique<FailHandler>());

  reg.dispatch("cmd.boom");

  REQUIRE(cap.lines.size() == 1);
  REQUIRE(cap.lines[0].find("[CMD]") != std::string::npos);
  REQUIRE(cap.lines[0].find("cmd.boom") != std::string::npos);
  REQUIRE(cap.lines[0].find("error") != std::string::npos);
}

TEST_CASE("Log functor receives [CMD] line for unknown command", "[registry]") {
  CapturingDeps cap;
  CommandRegistry reg(cap.make());

  reg.dispatch("no.such.command");

  REQUIRE(cap.lines.size() == 1);
  REQUIRE(cap.lines[0].find("[CMD]") != std::string::npos);
  REQUIRE(cap.lines[0].find("error") != std::string::npos);
}

// ── stringify ─────────────────────────────────────────────────────────────────

TEST_CASE("stringify produces expected format for success", "[result]") {
  REQUIRE(stringify("my.cmd", success()) == "[CMD] my.cmd -> ok");
}

TEST_CASE("stringify produces expected format for failure", "[result]") {
  REQUIRE(stringify("my.cmd", failure("bad input")) == "[CMD] my.cmd -> error: bad input");
}
