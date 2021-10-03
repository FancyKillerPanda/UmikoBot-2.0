#include "GlobalModule.h"
#include "UmikoBot.h"
#include "core/Permissions.h"

#include <Discord/Objects/Embed.h>

using namespace Discord;

// TODO(fkp): Move these later
#define OPTIONAL(x) "(" x ")?"
#define IDENTIFIER "\\s\\S+"
#define TEXT "\\s.+"

GlobalModule::GlobalModule()
	: Module("Global")
{
	registerCommand(Commands::Help, "help" OPTIONAL(IDENTIFIER), CommandPermission::User, help);
	registerCommand(Commands::Echo, "echo" TEXT, CommandPermission::User, echo);
	registerCommand(Commands::SetPrefix, "set-prefix" IDENTIFIER, CommandPermission::Moderator, setPrefix);
}

GlobalModule::~GlobalModule()
{
}

void GlobalModule::help(Module* module, const Discord::Message& message, const Discord::Channel& channel)
{
	QStringList args = message.content().split(QRegularExpression("\\s"));
	QString prefix = UmikoBot::get().getGuildData()[channel.guildId()].prefix;
	QString output = "";
	
	// TODO(fkp): Separate admin commands
	if (args.size() == 1)
	{
		for (Module* module : UmikoBot::get().getModules())
		{
			output += "**" + module->getName() + "**\n";
			
			for (const Command& command : module->getCommands())
			{
				const QString& description = CommandInfo::briefDescription[(unsigned int) command.id];
				output += QString("`%1%2` - %3\n").arg(prefix, command.name, description);
			}

			output += "\n";
		}
	}
	else
	{
		const QString& request = args[1];
		for (Module* module : UmikoBot::get().getModules())
		{
			for (const Command& command : module->getCommands())
			{
				if (command.name == request)
				{
					const QString& description = CommandInfo::briefDescription[(unsigned int) command.id];
					const QString& usage = CommandInfo::usage[(unsigned int) command.id];
					const QString& additionalInfo = CommandInfo::additionalInfo[(unsigned int) command.id];
					bool adminRequired = CommandInfo::adminRequired[(unsigned int) command.id];

					output += "**Command name:** `" + command.name + "`\n\n";
					output += description + "\n" + additionalInfo + "\n";
					if (additionalInfo != "")
					{
						output += "\n";
					}
					output += "**Usage:**`" + prefix + usage + "`";
				}
			}
		}

		if (output == "")
		{
			SEND_MESSAGE("I could not find that command!");
			return;
		}
	}

	Embed embed {};
	embed.setColor(qrand() % 0xffffff);
	embed.setTitle("Help");
	embed.setDescription(output);
	SEND_MESSAGE(embed);
}

void GlobalModule::echo(Module* module, const Discord::Message& message, const Discord::Channel& channel)
{
	QString restOfMessage = message.content().mid(message.content().indexOf(QRegularExpression("\\s")));
	SEND_MESSAGE(restOfMessage);
}

void GlobalModule::setPrefix(Module* module, const Discord::Message& message, const Discord::Channel& channel)
{
	QStringList args = message.content().split(QRegularExpression("\\s"));
	QString& prefix = UmikoBot::get().getGuildData()[channel.guildId()].prefix;

	if (prefix == args[1])
	{
		SEND_MESSAGE("That is already the prefix!");
	}
	else
	{
		prefix = args[1];
		SEND_MESSAGE(QString("Changed prefix to '%1'").arg(prefix));
	}
}
