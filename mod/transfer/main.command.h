#include "../command/command.h"
#include "../command/parameter.h"

using namespace BDL::CustomCommand;

class TransferCommand : public CustomCommandContext {
	// 都给我听着！下面这玩意你可以改，出了问题别怪我
		// static constexpr auto description = "从一个服务器链接到另一服务器";
	
public:
  static constexpr auto cmd_name    = "transfer";
  static constexpr auto description = "Transfer you to server";
  static constexpr auto permission  = CommandPermissionLevel::NORMAL;

  TransferCommand(CommandOrigin const &origin, CommandOutput &output) noexcept : CustomCommandContext(origin, output) {}

  void invoke(mandatory<std::string> server, mandatory<int> port);
};

command_register_function register_commands();
