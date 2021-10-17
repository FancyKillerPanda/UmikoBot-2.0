#include <stdio.h>

#include <QJsonDocument>
#include <QJsonValue>
#include <QFile>
#include <QDir>

#include "UmikoBot.h"
#include "core/Permissions.h"
#include "modules/GlobalModule.h"
#include "modules/CurrencyModule.h"

using namespace Discord;

UmikoBot& UmikoBot::get()
{
	static UmikoBot bot;
	return bot;
}

UmikoBot::UmikoBot(QObject* parent)
	: Client("umiko-bot", parent)
{
	printf("Starting bot...\n");
	QDir().mkdir("configs");
	qsrand(QTime::currentTime().msec());

	saveTimer.setInterval(60 * 1000);
	QObject::connect(&saveTimer, &QTimer::timeout, [this]()
	{
		save();
	});

	// Event callback connections
	connect(this, &Client::onReady, this, &UmikoBot::umikoOnReady);
	connect(&getGatewaySocket(), &GatewaySocket::disconnected, this, &UmikoBot::umikoOnDisconnect);

	connect(this, &Client::onGuildCreate, this, &UmikoBot::umikoOnGuildCreate);
	connect(this, &Client::onGuildUpdate, this, &UmikoBot::umikoOnGuildUpdate);

	connect(this, &Client::onGuildRoleUpdate, this, &UmikoBot::umikoOnGuildRoleUpdate);
	connect(this, &Client::onGuildRoleDelete, this, &UmikoBot::umikoOnGuildRoleDelete);

	connect(this, &Client::onGuildMemberAdd, this, &UmikoBot::umikoOnGuildMemberAdd);
	connect(this, &Client::onGuildMemberUpdate, this, &UmikoBot::umikoOnGuildMemberUpdate);
	connect(this, &Client::onGuildMemberRemove, this, &UmikoBot::umikoOnGuildMemberRemove);
	connect(this, &Client::onMessageCreate, this, &UmikoBot::umikoOnMessageCreate);

	// Modules
	modules.push_back(new GlobalModule());
	modules.push_back(new CurrencyModule());
}

UmikoBot::~UmikoBot()
{
	save();
	printf("Stopping bot...\n");
}

void UmikoBot::save()
{
	saveGuildData();

	for (Module* module : modules)
	{
		module->save();
	}
}

void UmikoBot::saveGuildData()
{
	QJsonObject json;

	for (const GuildData& data : guildData.values())
	{
		json[QString::number(data.guildId)] = data.writeToObject();
	}

	QJsonDocument doc { json };
	QString result = doc.toJson(QJsonDocument::Indented);

	// Writes the JSON out to file
	QFile file { SETTINGS_LOCATION };
	if (!file.open(QIODevice::WriteOnly))
	{
		printf("Unable to open file for saving: \"%s\"\n", SETTINGS_LOCATION);
		return;
	}
	
	file.write(qPrintable(result));
	file.close();
}

void UmikoBot::load()
{
	loadGuildData();

	// Parses the full enum string into separate commands
	QStringList individialCommands = CommandInfo::enumFullString.split(",");
	for (unsigned int i = 0; i < (unsigned int) Commands::Count; i++)
	{
		CommandInfo::commandStrings[i] = individialCommands[i].trimmed();
	}

	// Loads the commands
	QFile file { "commands.json" };
	if (!file.open(QIODevice::ReadOnly))
	{
		printf("Failed to open commands.json for reading...\n");
		return;
	}

	QByteArray data = file.readAll();
	QJsonDocument doc = QJsonDocument::fromJson(data);
	QJsonObject json = doc.object();

	for (unsigned int i = 0; i < (unsigned int) Commands::Count; i++)
	{
		QJsonValue commandJson = json[CommandInfo::commandStrings[i]];

		// Would be undefined if the command has no description
		if (commandJson != QJsonValue::Undefined)
		{
			QJsonObject current = commandJson.toObject();
			
			CommandInfo::briefDescription[i] = current["brief"].toString();
			CommandInfo::usage[i] = current["usage"].toString();
			CommandInfo::additionalInfo[i] = current["additional"].toString();
			CommandInfo::adminRequired[i] = current["admin"].toBool();
		}
	}

	file.close();

	// Loads the modules
	for (Module* module : modules)
	{
		module->load();
	}
}

void UmikoBot::loadGuildData()
{
	QFile file { SETTINGS_LOCATION };
	if (!file.open(QIODevice::ReadOnly))
	{
		printf("Unable to open file for loading: \"%s\"\n", SETTINGS_LOCATION);
		return;
	}

	QByteArray fileData = file.readAll();
	QJsonDocument doc = QJsonDocument::fromJson(fileData);
	QJsonObject json = doc.object();
	QStringList guildIds = json.keys();

	for (const QString& guildId : guildIds)
	{
		QJsonObject current = json.value(guildId).toObject();
		guildData[guildId.toULongLong()] = GuildData::createFromObject(guildId.toULongLong(), current);
	}

	file.close();
}

void UmikoBot::initialiseGuilds(GuildId afterId)
{
	constexpr snowflake_t LIMIT = 100;
	
	getCurrentUserGuilds(0, afterId, LIMIT).then([this](const QList<Guild>& guilds)
	{
		for (const Guild& guild : guilds)
		{
			getGuild(guild.id()).then([this](const Guild& guild)
			{
				guildData[guild.id()].ownerId = guild.ownerId();
			});

			getGuildRoles(guild.id()).then([this, guild](const QList<Role>& roles)
			{
				guildData[guild.id()].roles = roles;
			});

			initialiseGuildMembers(guild.id());
		}

		if (guilds.size() == LIMIT)
		{
			// More to come
			initialiseGuilds(guilds.back().id());
		}
		else
		{
			printf("Guild count: %d\n", guilds.size());
		}
	});
}

void UmikoBot::initialiseGuildMembers(GuildId guildId, UserId afterId)
{
	constexpr snowflake_t LIMIT = 1000;
	
	listGuildMembers(guildId, LIMIT, afterId).then([this, guildId](const QList<GuildMember>& members)
	{
		for (const GuildMember& member : members)
		{
			guildData[guildId].userData[member.user().id()].username = member.user().username();
			guildData[guildId].userData[member.user().id()].nickname = member.nick();
		}

		if (members.size() == LIMIT)
		{
			// More to come
			initialiseGuildMembers(guildId, members.back().user().id());
		}
		
		printf("Guild %llu: %d members\n", guildId, members.size());
	});
}

bool UmikoBot::isOwner(GuildId guildId, UserId userId)
{
	return guildData[guildId].ownerId == userId;
}

const QList<Discord::Role>& UmikoBot::getRoles(GuildId guildId)
{
	return guildData[guildId].roles;
}

const QString& UmikoBot::getNickname(GuildId guildId, UserId userId)
{
	return guildData[guildId].userData[userId].nickname;
}

const QString& UmikoBot::getUsername(GuildId guildId, UserId userId)
{
	return guildData[guildId].userData[userId].username;
}

const QString& UmikoBot::getName(GuildId guildId, UserId userId)
{
	if (getNickname(guildId, userId) != "")
	{
		return getNickname(guildId, userId);
	}

	return getUsername(guildId, userId);
}

Promise<QString>& UmikoBot::getAvatar(GuildId guildId, UserId userId)
{
	Promise<QString>* promise = new Promise<QString>();

	getGuildMember(guildId, userId).then([promise, userId](const GuildMember& member)
	{
		QString icon = member.user().avatar();
		if (icon != "")
		{
			icon = "https://cdn.discordapp.com/avatars/" + QString::number(userId) + "/" + icon + ".png";
		}
		else
		{
			icon = "https://cdn.discordapp.com/embed/avatars/" + QString::number(member.user().discriminator().toULongLong() % 5) + ".png";
		}

		promise->resolve(icon);
	});

	return *promise;
}

UserId UmikoBot::getUserIdFromArgument(GuildId guildId, const QString& argument)
{
	UserId result;
	
	// Direct mention
	result = getUserIdFromMention(guildId, argument);
	if (result) return result;

	// User ID
	result = argument.toULongLong();
	if (result != 0)
	{
		if (getName(guildId, result) != "")
		{
			return result;
		}

		// Not valid, let's reset
		result = 0;
	}

	// Checks for a username/nickname match
	UserId completeNickname = 0;
	UserId partialUsername = 0;
	UserId partialNickname = 0;
	
	for (auto it = guildData[guildId].userData.begin(); it != guildData[guildId].userData.end(); it++)
	{
		if (argument == it.value().username)
		{
			return it.key();
		}

		if (argument == it.value().nickname)
		{
			completeNickname = it.key();
		}
		else if (it.value().username.startsWith(argument))
		{
			partialUsername = it.key();
		}
		else if (it.value().nickname.startsWith(argument))
		{
			partialNickname = it.key();
		}
	}

	if (completeNickname) return completeNickname;
	if (partialUsername) return partialUsername;
	if (partialNickname) return partialNickname;
	
	return 0;
}

UserId UmikoBot::getUserIdFromMention(GuildId guildId, const QString& mention)
{
	static const QRegularExpression mentionRegex { "^<@!?([0-9]+)>$" };
	QRegularExpressionMatch match = mentionRegex.match(mention);

	if (match.hasMatch())
	{
		UserId userId = match.captured(1).toULongLong();
		if (getName(guildId, userId) != "")
		{
			return userId;
		}
	}

	return 0;
}

Promise<Channel>& UmikoBot::getChannelFromArgument(GuildId guildId, const QString& argument)
{
	Promise<Channel>* promise = new Promise<Channel>();
	getGuildChannels(guildId).then([this, argument, promise](const QList<Channel>& channels)
	{
		static const QRegularExpression channelMentionRegex { "^<#!?([0-9]+)>$" };
		QRegularExpressionMatch match = channelMentionRegex.match(argument);

		if (match.hasMatch())
		{
			ChannelId channelId = match.captured(1).toULongLong();
			for (const Channel& channel : channels)
			{
				if (channel.id() == channelId)
				{
					promise->resolve(channel);
					return;
				}
			}
		}
		else
		{
			for (const Channel& channel : channels)
			{
				if (channel.name() == argument)
				{
					promise->resolve(channel);
					return;
				}
			}
		}
		
		promise->reject();
	});

	return *promise;
}

void UmikoBot::umikoOnReady()
{
	printf("Ready!\n");

	load();
	initialiseGuilds();
}

void UmikoBot::umikoOnDisconnect()
{
}

void UmikoBot::umikoOnGuildCreate(const Guild& guild)
{
	guildData[guild.id()] = GuildData { guild.id() };
	initialiseGuilds(); // TODO(fkp): Only initialise a single guild?
}

void UmikoBot::umikoOnGuildUpdate(const Guild& guild)
{
	guildData[guild.id()].ownerId = guild.ownerId();
}

void UmikoBot::umikoOnGuildRoleUpdate(GuildId guildId, const Role& newRole)
{
	for (Role& role : guildData[guildId].roles)
	{
		if (role.id() == newRole.id())
		{
			role = newRole;
			return;
		}
	}

	guildData[guildId].roles.push_back(newRole);
}

void UmikoBot::umikoOnGuildRoleDelete(GuildId guildId, RoleId roleId)
{
	for (int i = 0; i < guildData[guildId].roles.size(); i++)
	{
		if (guildData[guildId].roles[i].id() == roleId)
		{
			guildData[guildId].roles.erase(guildData[guildId].roles.begin() + i);
			return;
		}
	}
}

void UmikoBot::umikoOnGuildMemberAdd(const GuildMember& member, GuildId guildId)
{
	guildData[guildId].userData[member.user().id()].username = member.user().username();
}

void UmikoBot::umikoOnGuildMemberUpdate(GuildId guildId, const QList<RoleId>& roles, const User& user, const QString& nickname)
{
	(void) roles;
	guildData[guildId].userData[user.id()].username = user.username();
	guildData[guildId].userData[user.id()].nickname = nickname;
}

void UmikoBot::umikoOnGuildMemberRemove(GuildId guildId, const User& user)
{
	for (auto it = guildData[guildId].userData.begin(); it != guildData[guildId].userData.end(); it++)
	{
		if (it.key() == user.id())
		{
			guildData[guildId].userData.erase(it);
			return;
		}
	}
}

void UmikoBot::umikoOnMessageCreate(const Message& message)
{
	getChannel(message.channelId()).then([this, message](const Channel& channel)
	{
		// messageString -> !status Name
		// prefix        -> !
		// fullCommand   ->  status Name
		// commandName   ->  status
		QString messageString = message.content();
		const QString& prefix = getGuildData()[channel.guildId()].prefix;
		QString fullCommand = messageString.mid(prefix.length());
		QString commandName = messageString.mid(prefix.length(), messageString.indexOf(QRegularExpression(SPACE)) - prefix.length());

		bool isCommand = false;

		if (messageString.startsWith(prefix))
		{
			for (Module* module : modules)
			{
				for (const Command& command : module->getCommands())
				{
					if (command.name == commandName)
					{
						isCommand = true;
						
						::Permissions::contains(channel.guildId(), message.author().id(), command.requiredPermissions,
												[this, message, channel, command, fullCommand, module, prefix](bool result)
						{
							if (!result)
							{
								SEND_MESSAGE("You do not have permission to use this command!");
								return;
							}

							if (command.regex.match(fullCommand).hasMatch())
							{
								if (command.enabled)
								{

									command.callback(message, channel);

								}
								else
								{
									SEND_MESSAGE("This command has been disabled!");
								}
							}
							else
							{
								// Looks for global module and outputs help text
								for (Module* globalModule : modules)
								{
									if (globalModule->getName() == "Global")
									{
										QString helpText = ((GlobalModule*) globalModule)->commandHelp(command.name, prefix);
										
										Embed embed {};
										embed.setColor(qrand() % 0xffffff);
										embed.setTitle("Incorrect Usage of Command");
										embed.setDescription(helpText);
										SEND_MESSAGE(embed);

										break;
									}
								}
							}
						});

						break;
					}
				}
			}
		}

		if (!isCommand)
		{
			for (Module* module : modules)
			{
				module->onMessage(message, channel);
			}
		}
	});
}
