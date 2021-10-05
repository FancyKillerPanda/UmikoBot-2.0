#include "CurrencyModule.h"
#include "UmikoBot.h"
#include "core/Permissions.h"

using namespace Discord;

CurrencyModule::CurrencyModule()
	: Module("Currency")
{
	dayTimer.setInterval(24 * 60 * 60 * 1000);
	dayTimer.start();
	QObject::connect(&dayTimer, &QTimer::timeout, [this]()
	{
		// Resets daily collection timeout and removes users no longer in the server
		for (snowflake_t guildId : currencyData.keys())
		{
			for (int userIndex = 0; userIndex < currencyData[guildId].size(); userIndex++)
			{
				UserCurrencyData& userCurrencyData = currencyData[guildId][userIndex];
				userCurrencyData.hasClaimedDaily = false;
				// TODO(fkp): Each module should be able to react to guild member remove
			}
		}
	});
	
	registerCommand(Commands::Daily, "daily", CommandPermission::User, daily);
	registerCommand(Commands::Wallet, "wallet", CommandPermission::User, wallet);
}

CurrencyModule::~CurrencyModule()
{
}

UserCurrencyData& CurrencyModule::getUserCurrencyData(snowflake_t guildId, snowflake_t userId)
{
	for (UserCurrencyData& userCurrencyData : currencyData[guildId])
	{
		if (userCurrencyData.userId == userId)
		{
			return userCurrencyData;
		}
	}

	// The user does not exist yet, make a new one
	currencyData[guildId].append(UserCurrencyData { userId });
	return currencyData[guildId].back();
}

void CurrencyModule::wallet(Module* module, const Discord::Message& message, const Discord::Channel& channel)
{
	CurrencyModule* self = (CurrencyModule*) module;
	QStringList args = message.content().split(QRegularExpression("\\s"));
	snowflake_t userId = 0;

	if (args.size() == 1)
	{
		userId = message.author().id();
	}

	UmikoBot::get().getAvatar(channel.guildId(), userId).then([self, message, channel, userId](const QString& icon)
	{
		const GuildCurrencyConfig& guildCurrencyConfig = self->currencyConfigs[channel.guildId()];
		QString desc = QString("Current %1s: **%2 %3**").arg(guildCurrencyConfig.currencyName,
															QString::number(self->getUserCurrencyData(channel.guildId(), userId).balance / 100.0f),
															guildCurrencyConfig.currencyAbbreviation);

		Embed embed;
		embed.setColor(qrand() % 0xffffff);
		embed.setAuthor(EmbedAuthor { UmikoBot::get().getName(channel.guildId(), userId) + "'s Wallet", "", icon });
		embed.setDescription(desc);
		SEND_MESSAGE(embed);
	});
}

void CurrencyModule::daily(Module* module, const Discord::Message& message, const Discord::Channel& channel)
{
	CurrencyModule* self = (CurrencyModule*) module;
	UserCurrencyData& userCurrencyData = self->getUserCurrencyData(channel.guildId(), message.author().id());
	const GuildCurrencyConfig& guildCurrencyConfig = self->currencyConfigs[channel.guildId()];

	if (userCurrencyData.hasClaimedDaily)
	{
		// TODO(fkp): Time left
		SEND_MESSAGE("You have already claimed your credits for today!");
		return;
	}

	userCurrencyData.balance += guildCurrencyConfig.rewardForDaily;
	userCurrencyData.hasClaimedDaily = true;
	
	QString output = QString("You now have **%1** more %2s in your wallet!").arg(QString::number(guildCurrencyConfig.rewardForDaily / 100.0f),
																				 guildCurrencyConfig.currencyName);
	SEND_MESSAGE(output);
}