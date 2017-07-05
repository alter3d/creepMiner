#include "Wallet.hpp"
#include "MinerUtil.hpp"
#include <Poco/Net/HTTPClientSession.h>
#include "MinerConfig.hpp"
#include "Request.hpp"
#include <Poco/Net/HTTPRequest.h>
#include <Poco/JSON/Parser.h>
#include <cassert>
#include <Poco/NestedDiagnosticContext.h>
#include "MinerLogger.hpp"
#include "Account.hpp"

using namespace Poco::Net;

Burst::Wallet::Wallet()
{}

Burst::Wallet::Wallet(const Url& url)
	: url_(url)
{
	poco_ndc(Wallet(const Url& url));
}

Burst::Wallet::~Wallet()
{}

bool Burst::Wallet::getWinnerOfBlock(uint64_t block, AccountId& winnerId) const
{
	return false;
}

bool Burst::Wallet::getNameOfAccount(AccountId account, std::string& name) const
{
	return false;
}

bool Burst::Wallet::getRewardRecipientOfAccount(AccountId account, AccountId& rewardRecipient) const
{
	return false;
}

bool Burst::Wallet::getLastBlock(uint64_t& block) const
{
	return false;
}

void Burst::Wallet::getAccount(AccountId id, Account& account) const
{
}

bool Burst::Wallet::isActive() const
{
	return false;
}

bool Burst::Wallet::sendWalletRequest(const Poco::URI& uri, Poco::JSON::Object::Ptr& json) const
{
	return false;
}
