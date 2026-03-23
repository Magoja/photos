#include <catch2/catch_test_macros.hpp>
#include "command/CommandRegistry.h"
#include "command/ICommandHandler.h"
#include "command/CommandResult.h"

using namespace command;

namespace {

// A simple handler that records the last params it received.
struct EchoHandler : ICommandHandler {
  nlohmann::json lastParams;
  CommandResult execute(nlohmann::json params) override {
    lastParams = params;
    return CommandResult::success(params);
  }
};

struct FailHandler : ICommandHandler {
  CommandResult execute(nlohmann::json /*params*/) override {
    return CommandResult::failure("always fails");
  }
};

}  // namespace

TEST_CASE("Dispatch unknown command returns failure with name in error", "[registry]") {
  CommandRegistry reg;
  const auto result = reg.dispatch("no.such.command");
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error.find("no.such.command") != std::string::npos);
}

TEST_CASE("Dispatch known command calls handler with params", "[registry]") {
  CommandRegistry reg;
  auto handler = std::make_shared<EchoHandler>();
  reg.registerHandler("test.echo", handler);

  const nlohmann::json params = {{"key", "value"}};
  const auto result = reg.dispatch("test.echo", params);

  REQUIRE(result.ok);
  REQUIRE(handler->lastParams["key"] == "value");
}

TEST_CASE("Multiple handlers are independent", "[registry]") {
  CommandRegistry reg;
  auto a = std::make_shared<EchoHandler>();
  auto b = std::make_shared<FailHandler>();
  reg.registerHandler("cmd.a", a);
  reg.registerHandler("cmd.b", b);

  REQUIRE(reg.dispatch("cmd.a").ok);
  REQUIRE_FALSE(reg.dispatch("cmd.b").ok);
  REQUIRE_FALSE(reg.dispatch("cmd.c").ok);  // unregistered
}

TEST_CASE("CommandResult factories", "[registry]") {
  const auto ok = CommandResult::success({{"x", 1}});
  REQUIRE(ok.ok);
  REQUIRE(ok.data["x"] == 1);
  REQUIRE(ok.error.empty());

  const auto fail = CommandResult::failure("bad input");
  REQUIRE_FALSE(fail.ok);
  REQUIRE(fail.error == "bad input");
}
