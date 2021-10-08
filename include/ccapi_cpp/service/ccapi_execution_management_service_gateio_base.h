#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_GATEIO_BASE_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_GATEIO_BASE_H_
#ifdef CCAPI_ENABLE_SERVICE_EXECUTION_MANAGEMENT
#if defined(CCAPI_ENABLE_EXCHANGE_GATEIO) || defined(CCAPI_ENABLE_EXCHANGE_GATEIO_PERPETUAL_FUTURES)
#include "ccapi_cpp/service/ccapi_execution_management_service.h"
namespace ccapi {
class ExecutionManagementServiceGateioBase : public ExecutionManagementService {
 public:
  ExecutionManagementServiceGateioBase(std::function<void(Event&, Queue<Event>*)> eventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                                       ServiceContextPtr serviceContextPtr)
      : ExecutionManagementService(eventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {}
  virtual ~ExecutionManagementServiceGateioBase() {}
#ifndef CCAPI_EXPOSE_INTERNAL

 protected:
#endif
  void signRequest(http::request<http::string_body>& req, const std::string& path, const std::string& queryString, const std::string& body,
                   const std::map<std::string, std::string>& credential) {
    auto apiSecret = mapGetWithDefault(credential, this->apiSecretName);
    std::string preSignedText = std::string(req.method_string());
    preSignedText += "\n";
    preSignedText += path;
    preSignedText += "\n";
    preSignedText += queryString;
    preSignedText += "\n";
    preSignedText += UtilAlgorithm::computeHash(UtilAlgorithm::ShaVersion::SHA512, body, true);
    preSignedText += "\n";
    preSignedText += req.base().at("TIMESTAMP").to_string();
    auto signature = Hmac::hmac(Hmac::ShaVersion::SHA512, apiSecret, preSignedText, true);
    req.set("SIGN", signature);
    req.target(queryString.empty() ? path : path + "?" + queryString);
    req.body() = body;
    req.prepare_payload();
  }
  void appendParam(std::string& queryString, const std::map<std::string, std::string>& param,
                   const std::map<std::string, std::string> standardizationMap = {}) {
    for (const auto& kv : param) {
      queryString += standardizationMap.find(kv.first) != standardizationMap.end() ? standardizationMap.at(kv.first) : kv.first;
      queryString += "=";
      queryString += Url::urlEncode(kv.second);
      queryString += "&";
    }
  }
  void appendSymbolId(std::string& queryString, const std::string& symbolId) {
    queryString += "currency_pair=";
    queryString += Url::urlEncode(symbolId);
    queryString += "&";
  }
  void appendParam(rj::Document& document, rj::Document::AllocatorType& allocator, const std::map<std::string, std::string>& param,
                   const std::map<std::string, std::string> standardizationMap = {
                       {CCAPI_EM_ORDER_SIDE, "side"},
                       {CCAPI_EM_ORDER_QUANTITY, "amount"},
                       {CCAPI_EM_ORDER_LIMIT_PRICE, "price"},
                       {CCAPI_EM_CLIENT_ORDER_ID, "text"},
                       {CCAPI_EM_ORDER_TYPE, "type"},
                       {CCAPI_EM_ACCOUNT_TYPE, "account"},
                   }) {
    for (const auto& kv : param) {
      auto key = standardizationMap.find(kv.first) != standardizationMap.end() ? standardizationMap.at(kv.first) : kv.first;
      auto value = kv.second;
      if (key == "side") {
        value = value == CCAPI_EM_ORDER_SIDE_BUY ? "buy" : "sell";
      }
      if (value != "null") {
        if (value == "true" || value == "false") {
          document.AddMember(rj::Value(key.c_str(), allocator).Move(), value == "true", allocator);
        } else {
          document.AddMember(rj::Value(key.c_str(), allocator).Move(), rj::Value(value.c_str(), allocator).Move(), allocator);
        }
      }
    }
  }
  void appendSymbolId(rj::Document& document, rj::Document::AllocatorType& allocator, const std::string& symbolId) {
    document.AddMember("currency_pair", rj::Value(symbolId.c_str(), allocator).Move(), allocator);
  }
  void convertRequestForRest(http::request<http::string_body>& req, const Request& request, const TimePoint& now, const std::string& symbolId,
                             const std::map<std::string, std::string>& credential) override {
    req.set("Accept", "application/json");
    req.set(beast::http::field::content_type, "application/json");
    auto apiKey = mapGetWithDefault(credential, this->apiKeyName);
    req.set("KEY", apiKey);
    req.set("TIMESTAMP", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()));
    switch (request.getOperation()) {
      case Request::Operation::CREATE_ORDER: {
        req.method(http::verb::post);
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        rj::Document document;
        document.SetObject();
        rj::Document::AllocatorType& allocator = document.GetAllocator();
        this->appendParam(document, allocator, param);
        this->appendSymbolId(document, allocator, symbolId);
        rj::StringBuffer stringBuffer;
        rj::Writer<rj::StringBuffer> writer(stringBuffer);
        document.Accept(writer);
        auto body = stringBuffer.GetString();
        this->signRequest(req, this->createOrderTarget, "", body, credential);
      } break;
      case Request::Operation::CANCEL_ORDER: {
        req.method(http::verb::delete_);
        std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        std::string id = param.find(CCAPI_EM_ORDER_ID) != param.end()          ? param.at(CCAPI_EM_ORDER_ID)
                         : param.find(CCAPI_EM_CLIENT_ORDER_ID) != param.end() ? param.at(CCAPI_EM_CLIENT_ORDER_ID)
                                                                               : "";
        std::string path = this->cancelOrderTarget;
        this->substituteParam(path, {
                                        {"{order_id}", id},
                                    });
        param.erase(CCAPI_EM_ORDER_ID);
        param.erase(CCAPI_EM_CLIENT_ORDER_ID);
        param.erase("order_id");
        std::string queryString;
        this->appendParam(queryString, param,
                          {
                              {CCAPI_EM_ACCOUNT_ID, "account"},
                          });
        this->appendSymbolId(queryString, symbolId);
        queryString.pop_back();
        this->signRequest(req, path, queryString, "", credential);
      } break;
      case Request::Operation::GET_ORDER: {
        req.method(http::verb::get);
        std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        std::string id = param.find(CCAPI_EM_ORDER_ID) != param.end()          ? param.at(CCAPI_EM_ORDER_ID)
                         : param.find(CCAPI_EM_CLIENT_ORDER_ID) != param.end() ? param.at(CCAPI_EM_CLIENT_ORDER_ID)
                                                                               : "";
        std::string path = this->getOrderTarget;
        this->substituteParam(path, {
                                        {"{order_id}", id},
                                    });
        param.erase(CCAPI_EM_ORDER_ID);
        param.erase(CCAPI_EM_CLIENT_ORDER_ID);
        param.erase("order_id");
        std::string queryString;
        this->appendParam(queryString, param,
                          {
                              {CCAPI_EM_ACCOUNT_ID, "account"},
                          });
        this->appendSymbolId(queryString, symbolId);
        queryString.pop_back();
        this->signRequest(req, path, queryString, "", credential);
      } break;
      case Request::Operation::GET_OPEN_ORDERS: {
        req.method(http::verb::get);
        std::string queryString;
        std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        param["status"] = "open";
        this->appendParam(queryString, param,
                          {
                              {CCAPI_EM_ACCOUNT_ID, "account"},
                          });
        this->appendSymbolId(queryString, symbolId);
        queryString.pop_back();
        this->signRequest(req, this->getOpenOrdersTarget, queryString, "", credential);
      } break;
      case Request::Operation::CANCEL_OPEN_ORDERS: {
        req.method(http::verb::delete_);
        std::string queryString;
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        this->appendParam(queryString, param,
                          {
                              {CCAPI_EM_ACCOUNT_ID, "account"},
                          });
        this->appendSymbolId(queryString, symbolId);
        queryString.pop_back();
        this->signRequest(req, this->getOpenOrdersTarget, queryString, "", credential);
      } break;
      case Request::Operation::GET_ACCOUNTS: {
        req.method(http::verb::get);
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        std::string queryString;
        this->appendParam(queryString, param,
                          {
                              {CCAPI_EM_ASSET, "currency"},
                          });
        if (!queryString.empty()) {
          queryString.pop_back();
        }
        this->signRequest(req, this->getAccountsTarget, queryString, "", credential);
      } break;
      default:
        this->convertRequestForRestCustom(req, request, now, symbolId, credential);
    }
  }
  std::vector<Element> extractOrderInfoFromRequest(const Request& request, const Request::Operation operation, const rj::Document& document) override {
    const std::map<std::string, std::pair<std::string, JsonDataType> >& extractionFieldNameMap = {
        {CCAPI_EM_ORDER_ID, std::make_pair("id", JsonDataType::STRING)},
        {CCAPI_EM_CLIENT_ORDER_ID, std::make_pair("client_oid", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_SIDE, std::make_pair("side", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_QUANTITY, std::make_pair("size", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_LIMIT_PRICE, std::make_pair("price", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_CUMULATIVE_FILLED_QUANTITY, std::make_pair("filled_size", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_CUMULATIVE_FILLED_PRICE_TIMES_QUANTITY, std::make_pair("executed_value", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_STATUS, std::make_pair("status", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_INSTRUMENT, std::make_pair("product_id", JsonDataType::STRING)}};
    std::vector<Element> elementList;
    if (operation == Request::Operation::GET_OPEN_ORDERS || operation == Request::Operation::CANCEL_OPEN_ORDERS) {
      for (const auto& x : document.GetArray()) {
        Element element;
        this->extractOrderInfo(element, x, extractionFieldNameMap);
        elementList.emplace_back(std::move(element));
      }
    } else {
      Element element;
      this->extractOrderInfo(element, document, extractionFieldNameMap);
      elementList.emplace_back(std::move(element));
    }
    return elementList;
  }
  std::vector<Element> extractAccountInfoFromRequest(const Request& request, const Request::Operation operation, const rj::Document& document) override {
    std::vector<Element> elementList;
    switch (request.getOperation()) {
      case Request::Operation::GET_ACCOUNTS: {
        for (const auto& x : document.GetArray()) {
          Element element;
          element.insert(CCAPI_EM_ASSET, x["currency"].GetString());
          element.insert(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING, x["available"].GetString());
          elementList.emplace_back(std::move(element));
        }
      } break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
    return elementList;
  }
  std::vector<std::string> createSendStringListFromSubscription(const WsConnection& wsConnection, const Subscription& subscription, const TimePoint& now,
                                                                const std::map<std::string, std::string>& credential) override {
    std::vector<std::string> sendStringList;
    auto apiKey = mapGetWithDefault(credential, this->apiKeyName);
    auto apiSecret = mapGetWithDefault(credential, this->apiSecretName);
    int time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto fieldSet = subscription.getFieldSet();
    auto instrumentSet = subscription.getInstrumentSet();
    for (const auto& field : fieldSet) {
      rj::Document document;
      document.SetObject();
      rj::Document::AllocatorType& allocator = document.GetAllocator();
      document.AddMember("time", rj::Value(time).Move(), allocator);
      std::string channel;
      if (field == CCAPI_EM_PRIVATE_TRADE) {
        channel = "spot.usertrades";
      } else if (field == CCAPI_EM_ORDER_UPDATE) {
        channel = "spot.orders";
      }
      document.AddMember("channel", rj::Value(channel.c_str(), allocator).Move(), allocator);
      document.AddMember("event", rj::Value("subscribe").Move(), allocator);
      rj::Value payload(rj::kArrayType);
      if (instrumentSet.empty()) {
        payload.PushBack(rj::Value("!all").Move(), allocator);
      } else {
        for (const auto& instrument : instrumentSet) {
          payload.PushBack(rj::Value(instrument.c_str(), allocator).Move(), allocator);
        }
      }
      document.AddMember("payload", payload, allocator);
      rj::Value auth(rj::kObjectType);
      auth.AddMember("method", rj::Value("api_key").Move(), allocator);
      auth.AddMember("KEY", rj::Value(apiKey.c_str(), allocator).Move(), allocator);
      std::string preSignedText = "channel=" + channel + "&event=subscribe&time=" + std::to_string(time);
      auto signature = Hmac::hmac(Hmac::ShaVersion::SHA512, apiSecret, preSignedText, true);
      auth.AddMember("SIGN", rj::Value(signature.c_str(), allocator).Move(), allocator);
      document.AddMember("auth", auth, allocator);
      rj::StringBuffer stringBuffer;
      rj::Writer<rj::StringBuffer> writer(stringBuffer);
      document.Accept(writer);
      std::string sendString = stringBuffer.GetString();
      sendStringList.push_back(sendString);
    }
    return sendStringList;
  }
  void onTextMessage(const WsConnection& wsConnection, const Subscription& subscription, const std::string& textMessage, const rj::Document& document,
                     const TimePoint& timeReceived) override {
    Event event = this->createEvent(subscription, textMessage, document, timeReceived);
    if (!event.getMessageList().empty()) {
      this->eventHandler(event, nullptr);
    }
  }
  Event createEvent(const Subscription& subscription, const std::string& textMessage, const rj::Document& document, const TimePoint& timeReceived) {
    Event event;
    std::vector<Message> messageList;
    Message message;
    message.setTimeReceived(timeReceived);
    message.setCorrelationIdList({subscription.getCorrelationId()});
    std::string eventType = document["event"].GetString();
    if (eventType == "update") {
      event.setType(Event::Type::SUBSCRIPTION_DATA);
      auto fieldSet = subscription.getFieldSet();
      auto instrumentSet = subscription.getInstrumentSet();
      std::string channel = document["channel"].GetString();
      std::string field;
      if (channel == "spot.usertrades") {
        field = CCAPI_EM_PRIVATE_TRADE;
      } else if (channel == "spot.orders") {
        field = CCAPI_EM_ORDER_UPDATE;
      }
      if (fieldSet.find(field) != fieldSet.end()) {
        for (const auto& x : document["result"].GetArray()) {
          std::string instrument = x["currency_pair"].GetString();
          if (instrumentSet.empty() || instrumentSet.find(instrument) != instrumentSet.end()) {
            if (field == CCAPI_EM_PRIVATE_TRADE) {
              Message message;
              message.setTime(UtilTime::makeTimePointMilli(UtilTime::divideMilli(x["create_time_ms"].GetString())));
              message.setTimeReceived(timeReceived);
              message.setCorrelationIdList({subscription.getCorrelationId()});
              message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE);
              std::vector<Element> elementList;
              Element element;
              element.insert(CCAPI_TRADE_ID, std::string(document["id"].GetString()));
              element.insert(CCAPI_EM_ORDER_ID, document["order_id"].GetString());
              element.insert(CCAPI_EM_ORDER_SIDE, std::string(document["side"].GetString()) == "buy" ? CCAPI_EM_ORDER_SIDE_BUY : CCAPI_EM_ORDER_SIDE_SELL);
              element.insert(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE, document["price"].GetString());
              element.insert(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE, document["amount"].GetString());
              std::string takerSide = document["side"].GetString();
              if (document.FindMember("taker_user_id") != document.MemberEnd()) {
                element.insert(CCAPI_EM_ORDER_SIDE, takerSide == "buy" ? CCAPI_EM_ORDER_SIDE_BUY : CCAPI_EM_ORDER_SIDE_SELL);
                element.insert(CCAPI_IS_MAKER, "0");
                element.insert(CCAPI_EM_ORDER_ID, document["taker_order_id"].GetString());
              } else if (document.FindMember("maker_user_id") != document.MemberEnd()) {
                element.insert(CCAPI_EM_ORDER_SIDE, takerSide == "sell" ? CCAPI_EM_ORDER_SIDE_BUY : CCAPI_EM_ORDER_SIDE_SELL);
                element.insert(CCAPI_IS_MAKER, "1");
                element.insert(CCAPI_EM_ORDER_ID, document["maker_order_id"].GetString());
              }
              element.insert(CCAPI_EM_ORDER_INSTRUMENT, instrument);
              elementList.emplace_back(std::move(element));
              message.setElementList(elementList);
              messageList.push_back(std::move(message));
            } else if (field == CCAPI_EM_ORDER_UPDATE) {
              Message message;
              message.setTime(UtilTime::makeTimePointMilli(UtilTime::divideMilli(x["update_time_ms"].GetString())));
              message.setTimeReceived(timeReceived);
              message.setCorrelationIdList({subscription.getCorrelationId()});
              message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE);
              std::map<std::string, std::pair<std::string, JsonDataType> > extractionFieldNameMap = {
                  {CCAPI_EM_ORDER_ID, std::make_pair("id", JsonDataType::STRING)},
                  {CCAPI_EM_CLIENT_ORDER_ID, std::make_pair("text", JsonDataType::STRING)},
                  {CCAPI_EM_ORDER_SIDE, std::make_pair("side", JsonDataType::STRING)},
                  {CCAPI_EM_ORDER_LIMIT_PRICE, std::make_pair("price", JsonDataType::STRING)},
                  {CCAPI_EM_ORDER_QUANTITY, std::make_pair("amount", JsonDataType::STRING)},
                  {CCAPI_EM_ORDER_REMAINING_QUANTITY, std::make_pair("left", JsonDataType::STRING)},
                  {CCAPI_EM_ORDER_CUMULATIVE_FILLED_QUANTITY, std::make_pair("filled_total", JsonDataType::STRING)},
                  {CCAPI_EM_ORDER_STATUS, std::make_pair("event", JsonDataType::STRING)},
                  {CCAPI_EM_ORDER_INSTRUMENT, std::make_pair("currency_pair", JsonDataType::STRING)},
              };
              Element info;
              this->extractOrderInfo(info, document, extractionFieldNameMap);
              std::vector<Element> elementList;
              elementList.emplace_back(std::move(info));
              message.setElementList(elementList);
              messageList.push_back(std::move(message));
            }
          }
        }
      }
    } else if (eventType == "subscribe") {
      bool hasError = document.HasMember("error") && !document["error"].IsNull();
      event.setType(Event::Type::SUBSCRIPTION_STATUS);
      message.setType(hasError ? Message::Type::SUBSCRIPTION_FAILURE : Message::Type::SUBSCRIPTION_STARTED);
      Element element;
      element.insert(hasError ? CCAPI_ERROR_MESSAGE : CCAPI_INFO_MESSAGE, textMessage);
      message.setElementList({element});
      messageList.push_back(std::move(message));
    }
    event.setMessageList(messageList);
    return event;
  }
};
} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_GATEIO_BASE_H_
