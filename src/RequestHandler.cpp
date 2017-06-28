﻿#include "RequestHandler.hpp"
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/FileStream.h>
#include "MinerLogger.hpp"
#include "MinerUtil.hpp"
#include <Poco/JSON/Object.h>
#include "MinerServer.hpp"
#include "Miner.hpp"
#include <Poco/NestedDiagnosticContext.h>
#include "Request.hpp"
#include "MinerConfig.hpp"
#include <Poco/Net/HTTPClientSession.h>
#include "PlotSizes.hpp"
#include <Poco/Logger.h>
#include <Poco/Base64Decoder.h>
#include <Poco/StreamCopier.h>
#include <Poco/StringTokenizer.h>

void Burst::TemplateVariables::inject(std::string& source) const
{
	for (const auto& var : variables)
		Poco::replaceInPlace(source, "%" + var.first + "%", var.second());
}

void Burst::NotFoundHandler::handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
	response.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
	response.send();
}

Burst::RootHandler::RootHandler(const TemplateVariables& variables)
	: variables_{&variables}
{}

void Burst::RootHandler::handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
	response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
	response.setChunkedTransferEncoding(true);
	auto& out = response.send();

	try
	{
		Poco::FileInputStream file{ "public/index.html", std::ios::in };
		std::string str(std::istreambuf_iterator<char>{file}, {});
		variables_->inject(str);

		out << str;
	}
	catch (Poco::Exception& exc)
	{
		log_error(MinerLogger::server, "Could not open public/index.html!");
		log_exception(MinerLogger::server, exc);
	}
}

Burst::AssetHandler::AssetHandler(const TemplateVariables& variables)
	: variables_{&variables}
{}

void Burst::AssetHandler::handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
	try
	{
		const auto relativePath = "public/" + request.getURI();
		Poco::Path path{ relativePath };
		Poco::FileInputStream file{relativePath, std::ios::in};
		std::string str(std::istreambuf_iterator<char>{file}, {});

		std::string mimeType = "text/plain";

		auto ext = path.getExtension();

		if (ext == "css")
			mimeType = "text/css";
		else if (ext == "js")
			mimeType = "text/javascript";
		else if (ext == "png")
			mimeType = "image/png";

		response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
		response.setChunkedTransferEncoding(true);
		response.setContentType(mimeType);
		auto& out = response.send();

		out << str;
	}
	catch (Poco::Exception& exc)
	{
		log_error(MinerLogger::server, "Webserver could not open 'public/%s'!", request.getURI());
		log_exception(MinerLogger::server, exc);
	}
}

void Burst::BadRequestHandler::handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
	response.setStatus(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
	response.setContentLength(0);
	response.send();
}

Burst::WebSocketHandler::WebSocketHandler(MinerServer* server)
	: server_{server}
{}

void Burst::WebSocketHandler::handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
	try
	{
		if (server_ == nullptr)
			return;

		server_->addWebsocket(std::make_unique<Poco::Net::WebSocket>(request, response));
	}
	catch (...)
	{}
}

Burst::MiningInfoHandler::MiningInfoHandler(Miner& miner)
	: miner_{&miner}
{ }

void Burst::MiningInfoHandler::handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
	poco_ndc(MiningInfoHandler::handleRequest);

	Poco::JSON::Object json;
	json.set("baseTarget", std::to_string(miner_->getBaseTarget()));
	json.set("generationSignature", miner_->getGensigStr());
	json.set("targetDeadline", miner_->getTargetDeadline());
	json.set("height", miner_->getBlockheight());

	try
	{
		std::stringstream ss;
		json.stringify(ss);
		auto jsonStr = ss.str();

		response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
		response.setContentLength(jsonStr.size());

		auto& output = response.send();
		output << jsonStr;
	}
	catch (Poco::Exception& exc)
	{
		log_error(MinerLogger::server, "Webserver could not send mining info! %s", exc.displayText());
		log_current_stackframe(MinerLogger::server);
	}
}

Burst::SubmitNonceHandler::SubmitNonceHandler(Miner& miner)
	: miner_{&miner}
{ }

void Burst::SubmitNonceHandler::handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
	poco_ndc(SubmitNonceHandler::handleRequest);

	try
	{
		std::string plotsHash = "";

		try
		{
			if (request.has(X_PlotsHash))
			{
				auto plotsHashEncoded = request.get(X_PlotsHash);
				Poco::URI::decode(plotsHashEncoded, plotsHash);
				PlotSizes::set(plotsHash, Poco::NumberParser::parseUnsigned64(request.get(X_Capacity)));
			}
		}
		catch (Poco::Exception&)
		{
			log_debug(MinerLogger::server, "The X-PlotsHash from the other miner is not a number! %s", request.get(X_PlotsHash));
		}

		Poco::URI uri{ request.getURI() };

		uint64_t accountId = 0;
		uint64_t nonce = 0;
		uint64_t deadline = 0;
		std::string plotfile = "";

		for (const auto& param : uri.getQueryParameters())
		{
			if (param.first == "accountId")
				accountId = Poco::NumberParser::parseUnsigned64(param.second);
			else if (param.first == "nonce")
				nonce = Poco::NumberParser::parseUnsigned64(param.second);
		}

		if (request.has(X_Plotfile))
		{
			auto plotfileEncoded = request.get(X_Plotfile);
			Poco::URI::decode(plotfileEncoded, plotfile);
		}

		if (request.has(X_Deadline))
			deadline = Poco::NumberParser::parseUnsigned64(request.get(X_Deadline));

		auto account = miner_->getAccount(accountId);

		if (account == nullptr)
			account = std::make_shared<Account>(accountId);

		if (plotfile.empty())
			plotfile = !plotsHash.empty() ? plotsHash : "unknown";

		log_information(MinerLogger::server, "Got nonce forward request (%s)\n"
			"\tnonce: %Lu\n"
			"\taccount: %s\n"
			"\tin: %s",
			deadlineFormat(deadline), nonce,
			account->getName().empty() ? account->getAddress() : account->getName(),
			plotfile
		);

		if (accountId != 0 && nonce != 0 && deadline != 0)
		{
			auto future = miner_->submitNonceAsync(std::make_tuple(nonce, accountId, deadline,
				miner_->getBlockheight(), plotfile));

			future.wait();

			auto forwardResult = future.data();

			response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
			response.setChunkedTransferEncoding(true);
			auto& responseData = response.send();

			responseData << forwardResult.json;
		}
		else
		{
			// sum up the capacity
			request.set("X-Capacity", std::to_string(PlotSizes::getTotal()));
			
			// forward the request to the pool
			ForwardHandler{MinerConfig::getConfig().createSession(HostType::Pool)}.handleRequest(request, response);
		}
	}
	catch (Poco::Exception& exc)
	{
		log_error(MinerLogger::server, "Could not forward nonce! %s", exc.displayText());
		log_current_stackframe(MinerLogger::server);
	}
}

Burst::ForwardHandler::ForwardHandler(std::unique_ptr<Poco::Net::HTTPClientSession> session)
	: session_{std::move(session)}
{ }

void Burst::ForwardHandler::handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
{
	if (session_ == nullptr)
		return;

	log_information(MinerLogger::server, "Forwarding request\n\t%s", request.getURI());

	try
	{
		Request forwardRequest{std::move(session_)};
		auto forwardResponse = forwardRequest.send(request);

		log_debug(MinerLogger::server, "Request forwarded, waiting for response...");

		std::string data;

		if (forwardResponse.receive(data))
		{
			log_debug(MinerLogger::server, "Got response, sending back...\n\t%s", data);

			response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
			response.setContentLength(data.size());

			auto& responseStream = response.send();
			responseStream << data;
		}
	}
	catch (Poco::Exception& exc)
	{
		log_error(MinerLogger::server, "Could not forward request to wallet!\n%s\n%s", exc.displayText(), request.getURI());
		log_current_stackframe(MinerLogger::server);
	}
}
