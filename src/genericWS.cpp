#include "genericWS.h"

#include <algorithm> // pour std::transform
#include <cctype>    // pour ::tolower
#include <cmath>    // pour round/floor
#include <sstream>  // pour std::istringstream
#include <iomanip>  // pour std::get_time
#include <ctime>    // pour std::tm, timegm (sur Linux/Mac)
#include <memory>
#include "tools.h"

// Constructor
GenericWebSocket::GenericWebSocket(
        std::string name, 
        std::vector<std::string> symbols, 
        int nsymbols, 
        ThreadSafeQueue<normalized_data>& output_queue) : datas_output(output_queue) { 
    this->name = name;
    this->symbols = symbols;
    this->nsymbols = nsymbols;

    // Load configuration from config.json
    std::ifstream configfile("config.json", std::ifstream::binary);
    configfile >> config;
    
    // Extract URL and request format from config
    this->url = config[name]["url"].asString();
    this->fmt = config[name]["request_format"]["currency_format"].asString();
    this->is_lowercase = config[name]["request_format"]["is_lowercase"].asBool();
    this->formated_symbols = reformat_symbols(symbols, nsymbols, fmt, is_lowercase);

    std::cout << "URL: " << this->url << std::endl;
}

std::vector<std::string> GenericWebSocket::reformat_symbols(std::vector<std::string> symbols, int nsymbols, std::string fmt, bool is_lowercase) {
    std::vector<std::string> reformatted_list;
    // On réserve la mémoire pour éviter les réallocations inutiles
    reformatted_list.reserve(symbols.size());

    for (const auto& original_ticker : symbols) {
        // 1. Gestion de la casse (Lowercase)
        std::string ticker = original_ticker;
        if (is_lowercase) {
            std::transform(ticker.begin(), ticker.end(), ticker.begin(), 
                [](unsigned char c){ return std::tolower(c); }
            );
        }

        // Sécurité : on ignore les tickers trop courts (< 6 chars comme "BTCUSD")
        if (ticker.length() < 6) {
            reformatted_list.push_back(original_ticker); // Ou gérer l'erreur autrement
            continue;
        }

        // 2. Extraction Base (3 premiers) et Quote (3 derniers)
        std::string base = ticker.substr(0, 3);
        std::string quote = ticker.substr(ticker.length() - 3);

        // 3. Remplacement dans le format (fmt)
        std::string result = fmt;

        // Premier remplacement : le premier '*' devient la Base (ex: BTC)
        size_t first_star = result.find('*');
        if (first_star != std::string::npos) {
            result.replace(first_star, 1, base);
        }

        // Deuxième remplacement : le prochain '*' devient la Quote (ex: USD)
        // Note : on cherche à nouveau car la string a changé de taille
        size_t second_star = result.find('*');
        if (second_star != std::string::npos) {
            result.replace(second_star, 1, quote);
        }

        GenericWebSocket::symbols_map[original_ticker] = result;
        GenericWebSocket::symbols_map[result] = original_ticker;

        reformatted_list.push_back(result);
    }

    return reformatted_list;
}

void GenericWebSocket::connect(){
    // Connexion to the websocket server
    try {
        GenericWebSocket::websocket.setUrl(GenericWebSocket::url);
        std::cout << "Connecting to " << GenericWebSocket::url << "..." << std::endl;

        // Setup a callback to be fired (in a background thread, watch out for race conditions !)
        // when a message or an event (open, close, error) is received
        GenericWebSocket::websocket.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            {
                // Handle the different message types
                if (msg->type == ix::WebSocketMessageType::Message)
                {
                    // Message parsing into JSON
                    Json::Value root;
                    Json::Reader reader;
                    
                    if (reader.parse(msg->str, root)) {
                        try {
                            // Normalize the JSON message
                            std::vector<normalized_data> datas = this->normalise_message(root);

                            for (const auto& data : datas) {
                                // Verify that the normalized data is valid (e.g., symbol is not empty)
                                if (!data.symbol.empty()) { 
                                    // std::cout << "Exchange: " << data.exchange 
                                    //         << " | " << data.symbol
                                    //         << " | " << data.price << std::endl;
                                            
                                    // Push into the output queue
                                    this->datas_output.push(data);
                                }
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "Normalization error : " << e.what() << std::endl;
                        }
                    } else {
                        std::cerr << "Error on message / Json format" << std::endl;
                    }
                }
                // Open the connection
                else if (msg->type == ix::WebSocketMessageType::Open)
                {
                    std::cout << "Connection established" << std::endl;
                    std::cout << "> " << std::flush;
                    // Subscribe to the specified symbols once the connection is open
                    this->subscribe();
                }
                // Handle errors
                else if (msg->type == ix::WebSocketMessageType::Error)
                {
                    // Maybe SSL is not configured properly
                    std::cout << "Connection error: " << msg->errorInfo.reason << std::endl;
                    std::cout << "> " << std::flush;
                }
            }
        });
        // Now that our callback is setup, we can start our background thread and receive messages
        GenericWebSocket::websocket.start();

    } catch (const std::exception& e) {
        std::cerr << "Error connecting to " << GenericWebSocket::url << ": " << e.what() << std::endl;
    }
}

void GenericWebSocket::send_subscribe_payload(Json::Value payload) {
    // Send subscription request
    ix::WebSocketSendInfo result = websocket.send(payload.toStyledString());

    if (result.success) {
        std::cout << "Subscribed to [?symbols?] on " << GenericWebSocket::name << std::endl;
    } else {
        std::cerr << "Subscription failed for " << GenericWebSocket::name << std::endl;
    }
}

void GenericWebSocket::subscribe(){
    // Prepare subscription payload
    Json::Value payload = GenericWebSocket::config[GenericWebSocket::name]["request_format"]["subscription_format"];
    std::string params_key = GenericWebSocket::config[GenericWebSocket::name]["request_format"]["params_key"].asString();
    
    // Convert vector to Json::Value array
    Json::Value symbols_array(Json::arrayValue);
    for (const auto& symbol : GenericWebSocket::formated_symbols) {
        symbols_array.append(symbol);
    }
    // Symbols associated to the params_key in the subscription payload
    payload[params_key] = symbols_array;

    // Send subscription request
    this->send_subscribe_payload(payload);
}


void GenericWebSocket::unsubscribe(){
    Json::Value payload = this->config[this->name]["request_format"]["unsubscription_format"];
    std::string params_key = this->config[this->name]["request_format"]["params_key"].asString();
    
    Json::Value symbols_array(Json::arrayValue);
    for (const auto& symbol : this->formated_symbols) {
        symbols_array.append(symbol);
    }
    payload[params_key] = symbols_array;
    
    // Check if WebSocket is open
    if (this->websocket.getReadyState() != ix::ReadyState::Open) {
        std::cerr << "WebSocket is not open. Cannot unsubscribe." << std::endl;
        return;
    }
    // Send unsubscription request
    this->send_subscribe_payload(payload);
}

void GenericWebSocket::unsubscribe(std::vector<std::string> std_fmt_symb){
    Json::Value payload = this->config[this->name]["request_format"]["unsubscription_format"];
    std::string params_key = this->config[this->name]["request_format"]["params_key"].asString();
    
    // Reformat symbols according to exchange requirements
    std::vector<std::string> formated_symbols = this->reformat_symbols(std_fmt_symb, std_fmt_symb.size(), this->fmt, this->is_lowercase);

    Json::Value symbols_array(Json::arrayValue);
    for (const auto& symbol : formated_symbols) {
        symbols_array.append(symbol);
    }
    payload[params_key] = symbols_array;
    
    // Check if WebSocket is open
    if (this->websocket.getReadyState() != ix::ReadyState::Open) {
        std::cerr << "WebSocket is not open. Cannot unsubscribe." << std::endl;
        return;
    }
    // Send unsubscription request
    this->send_subscribe_payload(payload);
}

// Binance Websocket
std::vector<normalized_data> BinanceWS::normalise_message(Json::Value message) {
    std::vector<normalized_data> results;
    if (message.isMember("e") && message["e"].asString() == BinanceWS::stream_type) {
        normalized_data data;
        data.exchange = BinanceWS::name;

        // Symbol parsing: remove last char to convert from e.g., BTCUSDT to BTCUSD
        // Special case for Binance
        std::string symbol = message["s"].asString();
        if (!symbol.empty()) {
            symbol.pop_back();
        }
        // Fill normalized_data struct
        data.symbol = symbol;
        data.price = std::stod(message["p"].asString());
        data.quantity = std::stod(message["q"].asString());
        data.timestamp = message["T"].asUInt64();
        data.arrival_time = std::time(nullptr);
        results.push_back(data);
    }
    return results; // Return empty data if message type does not match
}

// Coinbase Websocket
std::vector<normalized_data> CoinbaseWS::normalise_message(Json::Value message) {
    std::vector<normalized_data> results;
    // Verify the existence of ("channel": "ticker")
    if (!message.isMember("channel") || message["channel"].asString() != "ticker") {
        return results; // Retourne un vecteur vide
    }
    // Verify the existence of events and tickers
    if (!message.isMember("events") || message["events"].empty() || 
        !message["events"][0].isMember("tickers") || message["events"][0]["tickers"].empty()) {
        return results;
    }
    Json::Value ticker_info = message["events"][0]["tickers"][0];
    normalized_data data;
    data.exchange = CoinbaseWS::name;
    
    std::string product_id = ticker_info["product_id"].asString();
    data.symbol = this->symbols_map[product_id].asString();
    
    data.price = std::stod(ticker_info["price"].asString());
    data.quantity = 0.0; // Comme dans votre code Python
    
    // Timestamp parsing
    // Received format : "2023-10-25T14:30:00.123456789Z"
    std::string ts_str = message["timestamp"].asString();
    // Parsing variables
    std::tm tm = {};
    double seconds_fraction = 0.0;
    // Separation into main time and fractional time
    size_t dot_pos = ts_str.find('.');
    std::string main_time_str;
    
    if (dot_pos != std::string::npos) {
        // Take the part befor the dot : "2023-10-25T14:30:00"
        main_time_str = ts_str.substr(0, dot_pos);
        // Parse the factionnal part : ex ".123456789Z"
        std::string fractional_part = ts_str.substr(dot_pos + 1);
        // Cleaning the fractional part from 'Z' or '+00:00' if present
        size_t z_pos = fractional_part.find_first_of("Z+");
        if (z_pos != std::string::npos) {
            fractional_part = fractional_part.substr(0, z_pos);
        }
        // Limit to microseconds (6 digits)
        if (fractional_part.length() > 6) {
            fractional_part = fractional_part.substr(0, 6);
        }
        // Convert to seconds fraction
        if (!fractional_part.empty()) {
            seconds_fraction = std::stod("0." + fractional_part);
        }
    } else {
        // No point found, just clean 'Z' or '+00:00'
        size_t z_pos = ts_str.find_first_of("Z+");
        main_time_str = (z_pos != std::string::npos) ? ts_str.substr(0, z_pos) : ts_str;
    }

    // Date/Hour parsing (YYYY-MM-DDTHH:MM:SS)
    std::istringstream ss(main_time_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    if (!ss.fail()) {
        // timegm convertit struct tm (UTC) en time_t (secondes depuis epoch)
        // Note: timegm est une extension GNU/BSD (dispo sur Mac/Linux). 
        // Sur Windows, c'est _mkgmtime(&tm).
        time_t time_seconds = timegm(&tm);
        
        // Final calculation in milliseconds
        data.timestamp = (static_cast<double>(time_seconds) + seconds_fraction) * 1000.0;
    } else {
        // Fallback when parsing fails
        data.timestamp = 0.0; 
    }

    data.arrival_time = std::time(nullptr);

    results.push_back(data);
    return results;
}

// Kraken Websocket
std::vector<normalized_data> KrakenWS::normalise_message(Json::Value message) {
    std::vector<normalized_data> results;

    // Filter out Heartbeats and Status messages
    // Kraken heartbeats are objects: {"event": "heartbeat"}
    // Ticker updates are arrays: [channelID, {data}, channelName, pair]
    if (!message.isArray()) {
        return results; 
    }

    // Validate array size (Ticker messages usually have 4 elements)
    if (message.size() < 4) {
        return results;
    }

    // Extract components based on fixed indices (Standard Kraken v1 format)
    // Format: [ <channelID>, <data_object>, <channel_name>, <pair> ]
    Json::Value payload = message[1];
    Json::Value channel_name = message[2];
    Json::Value pair_node = message[3];

    // Ensure we are processing a "ticker" message and data is valid
    if (channel_name.asString() != "ticker" || !payload.isObject()) {
        return results;
    }

    // Check for Close price ("c")
    if (!payload.isMember("c")) {
        return results;
    }

    Json::Value close_data = payload["c"];
    if (!close_data.isArray() || close_data.size() < 2) {
        return results;
    }

    // Normalize Symbol
    std::string kraken_symbol = pair_node.asString();
    std::string symbol;

    // Handle XBT -> BTC normalization
    if (kraken_symbol == "XBT/USD") {
        symbol = "BTCUSD";
    } else {
        // Remove the slash for standard format if needed, or use your map
        // If the map lookup fails, fallback to the raw string to prevent crashes
        if (this->symbols_map.isMember(kraken_symbol)) {
            symbol = this->symbols_map[kraken_symbol].asString();
        } else {
            symbol = kraken_symbol; 
            // Optional: Manually remove '/' if not in map
            // symbol.erase(std::remove(symbol.begin(), symbol.end(), '/'), symbol.end());
        }
    }

    normalized_data data;
    data.exchange = KrakenWS::name;
    data.symbol = symbol;
    
    // Kraken "c" field: [Price, Whole Lot Volume]
    // Use try/catch or safe conversions if using a strict JSON parser
    data.price = std::stod(close_data[0].asString());
    data.quantity = std::stod(close_data[1].asString());
    
    // Kraken v1 Ticker does NOT provide a trade timestamp in the "c" field.
    // It provides a time field, but often it is better to use local time for arrival.
    // If you need the exact match time, you must subscribe to the 'trade' channel, not 'ticker'.
    data.timestamp = std::time(nullptr); 
    data.arrival_time = std::time(nullptr);

    results.push_back(data);

    return results;
}

// OKX Websocket
Json::Value OkxWS::get_args_payload() {
    Json::Value args(Json::arrayValue); // Initialize as a JSON Array
    
    for (const auto& symbol : this->formated_symbols) {
        Json::Value arg;
        arg["channel"] = "trades";
        arg["instId"] = symbol;
        args.append(arg); // Use .append() for JSON arrays
    }
    return args;
}

void OkxWS::subscribe() {
    Json::Value payload;
    payload["op"] = "subscribe";
    payload["args"] = this->get_args_payload(); 
    // Send subscription request
    this->send_subscribe_payload(payload);
}

void OkxWS::unsubscribe() {
    Json::Value payload;
    payload["op"] = "unsubscribe";
    payload["args"] = this->get_args_payload();
    // Check if WebSocket is open
    if (this->websocket.getReadyState() != ix::ReadyState::Open) {
        std::cerr << "WebSocket is not open. Cannot unsubscribe." << std::endl;
        return;
    }
    // Send unsubscription request
    this->send_subscribe_payload(payload);
}

void OkxWS::unsubscribe(std::vector<std::string> std_fmt_symb) {
    std::vector<std::string> reformated_symbols = this->reformat_symbols(std_fmt_symb, std_fmt_symb.size(), this->fmt, this->is_lowercase);
    Json::Value args(Json::arrayValue); // Initialize as a JSON Array
    
    for (const auto& symbol : this->formated_symbols) {
        Json::Value arg;
        arg["channel"] = "trades";
        arg["instId"] = symbol;
        args.append(arg); // Use .append() for JSON arrays
    }

    // Prepare unsubscription payload
    Json::Value payload;
    payload["op"] = "unsubscribe";
    payload["args"] = args;

    // Check if WebSocket is open
    if (this->websocket.getReadyState() != ix::ReadyState::Open) {
        std::cerr << "WebSocket is not open. Cannot unsubscribe." << std::endl;
        return;
    }
    // Send unsubscription request
    this->send_subscribe_payload(payload);
}

std::vector<normalized_data> OkxWS::normalise_message(Json::Value message) {
    std::vector<normalized_data> results;
    if (message.get("arg", Json::Value::null).get("channel", "").asString() == "trades" && message.isMember("data")) {
        Json::Value trade_info = message["data"][0];
        
        for (const auto& trade : message["data"]) {
            normalized_data data;
            data.exchange = OkxWS::name;
            data.symbol = this->symbols_map[trade_info["instId"].asString()].asString();
            data.price = std::stod(trade_info["px"].asString());
            data.quantity = std::stod(trade_info["sz"].asString());
            data.timestamp = std::stod(trade_info["ts"].asString());
            data.arrival_time = std::time(nullptr);
            results.push_back(data);
        }
    }
    return results;
}

// Bybit Websocket
std::vector<normalized_data> BybitWS::normalise_message(Json::Value message) {
    std::vector<normalized_data> results;
    if (message.get("op", "").asString() == "subscribe") {
        return results;
    }

    if (BybitWS::symbols_map[message["topic"].asString()]) {
        normalized_data data;
        Json::Value trade = message["data"];
        data.exchange = BybitWS::name;
        data.symbol = this->symbols_map[message["topic"].asString()].asString();
        data.price = std::stod(trade["lastPrice"].asString());
        data.quantity = std::stod(trade["volume24h"].asString());
        data.timestamp = message["ts"].asUInt64();
        data.arrival_time = std::time(nullptr);
        results.push_back(data);
    }
    return results;
}