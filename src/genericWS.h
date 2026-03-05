#ifndef GENERICWS_HPP
#define GENERICWS_HPP
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXUserAgent.h>
#include <json/value.h>
#include <json/json.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cassert>

#include "struct.h"
#include "tools.h"

/**
 * @class GenericWebSocket
 * @brief Base class for WebSocket connections to cryptocurrency exchanges
 * 
 * This class provides a generic interface for connecting to WebSocket servers
 * of various cryptocurrency exchanges. It handles connection management,
 * subscription to market data, and message listening. Derived classes must
 * implement the normalise_message function to convert raw messages into a
 * standardized format.
 */
class GenericWebSocket {
    
    private:
        ThreadSafeQueue<normalized_data>& datas_output;    // Output queue for normalized data to be consumed by other parts of the system
    
    public:
        std::string name;        // Name of the exchange
        std::vector<std::string> symbols; // List of symbols to subscribe to
        std::string url;

        // Map of parsed symbols to their original format and vice versa
        Json::Value symbols_map;
        
        int nsymbols;       // Number of symbols
        virtual ~GenericWebSocket() = default;
        GenericWebSocket(
                std::string name, 
                std::vector<std::string> symbols, 
                int nsymbols, 
                ThreadSafeQueue<normalized_data>& output_queue);

        /**
         * @brief Normalize incoming messages
         * 
         * This pure virtual function takes a raw message from the websocket server
         * and normalizes it into a standard format defined by the normalized_data struct.
         * Each derived class must implement this function according to the specific
         * message format of the exchange it represents.
         * 
         * @param message Raw message received from the websocket server
         * 
         * @return normalized_data Normalized data structure
         */
        virtual std::vector<normalized_data> normalise_message(Json::Value message) = 0;

        /**
         * @brief Connect to the websocket server
         * 
         * This function etablishes a connection to the websocket server
         * and subscribes to the specified symbols. Print an error message if the connection fails.
         */
        void connect();
    
    protected:
        int N = 0; // Number of messages received
        bool is_running = false; // Is the websocket running

        std::string fmt;
        std::vector<std::string> formated_symbols;
        bool is_lowercase;

        Json::Value config; // Configuration loaded for Websockets
        ix::WebSocket websocket; // Websocket instance

        /**
         * @brief Reformat symbols to match exchange requirements
         * 
         * This function reformats symbols to match the exchange's requirements. It uses
         * the provided format string to create the symbols in the required format.
         * It can also convert the symbols to lowercase if specified.
         * 
         * Example:
         * reformat_symbols(["BTCUSD", "ETHUSD"], 2, "**t@aggTrade", true)
         * >>> ["btcusdt@aggtrade", "ethusdt@aggtrade"]
         * 
         * @param symbols Array of symbols in the standard format (e.g., ["BTCUSD", "ETHUSD"])
         * @param nsymbols Number of symbols in the array
         * @param fmt Format string to use for reformatting
         * @param is_lowercase Whether to convert symbols to lowercase
         * 
         * @return string* of reformatted symbols
         */
        std::vector<std::string> reformat_symbols(std::vector<std::string> symbols, int nsymbols, std::string fmt, bool is_lowercase);

        /**
         * @brief Listen for incoming messages
         * 
         * This function listens for incoming messages from the websocket server
         * and processes them using the normalise_message function. Manages the different
         * cases of received types messages (List, error, json object) and put the normalized datas into
         * the output queue.
         * Try to reconnect if the connection is lost. Can print error if the error is critical.
         */
        void listen();

        /**
         * @brief Close properly the webscoket connection
         */
        void close();

        /**
         * @brief Subscribe to the specified symbols
         * 
         * This function sends a request to the websocket server to subscribe to the specified symbols. 
         * It uses the request format from the configuration file to format the subscription request.
         */
        virtual void subscribe();

        /**
         * @brief Send the subscription payload
         * 
         * This function sends the subscription payload to the websocket server.
         * 
         * @param payload The subscription payload to send
         */
        void send_subscribe_payload(Json::Value payload);

        /**
         * @brief Unsubscribe from the specified symbols
         * 
         * This function sends a request to the websocket server to unsubscribe from the specified symbols. 
         * It uses the request format from the configuration file to format the unsubscribe request.
         * 
         * @param std_fmt_symb Array of symbols in the standard format (e.g., ["BTCUSD", "ETHUSD"])
         */
        virtual void unsubscribe(std::vector<std::string> std_fmt_symb);

        /**
         * @brief Unsubscribe from all symbols
         * 
         * This function sends a request to the websocket server to unsubscribe from all symbols currently subscribed.
         * It uses the request format from the configuration file to format the unsubscribe request.
         */
        virtual void unsubscribe();
};

class BinanceWS : public GenericWebSocket {
    public:
        BinanceWS(std::vector<std::string> symbols, int nsymbols, ThreadSafeQueue<normalized_data>& output_queue) : 
            GenericWebSocket("Binance", symbols, nsymbols, output_queue) {}
        std::string stream_type = "aggTrade"; // Type of stream to subscribe to
        std::vector<normalized_data> normalise_message(Json::Value message) override;
};

class CoinbaseWS : public GenericWebSocket {
    public:
        CoinbaseWS(std::vector<std::string> symbols, int nsymbols, ThreadSafeQueue<normalized_data>& output_queue) : 
            GenericWebSocket("Coinbase", symbols, nsymbols, output_queue) {}
        std::vector<normalized_data> normalise_message(Json::Value message) override;
};

class KrakenWS : public GenericWebSocket {
    public:
        KrakenWS(std::vector<std::string> symbols, int nsymbols, ThreadSafeQueue<normalized_data>& output_queue) : 
            GenericWebSocket("Kraken", symbols, nsymbols, output_queue) {}
        std::vector<normalized_data> normalise_message(Json::Value message) override;
};

class OkxWS : public GenericWebSocket {
    private:
        Json::Value args_payload;
        /**
         * @brief Return the args payload for subscription
         * 
         * This function constructs and returns the 'args' payload required for subscribing
         * to the OKX websocket channels. It formats the symbols into a Json::Value according to OKX's requirements.
         * @return A Json::Value The constructed 'args' payload
         */
        Json::Value get_args_payload();

    public:
        OkxWS(std::vector<std::string> symbols, int nsymbols, ThreadSafeQueue<normalized_data>& output_queue) : 
            GenericWebSocket("OKX", symbols, nsymbols, output_queue) {}
        std::vector<normalized_data> normalise_message(Json::Value message) override;
        // These methods are overridden because OKX has a specific subscription format
        void subscribe() override;
        void unsubscribe(std::vector<std::string> std_fmt_symb) override;
        void unsubscribe() override;

};

class BybitWS : public GenericWebSocket {
    public:
        BybitWS(std::vector<std::string> symbols, int nsymbols, ThreadSafeQueue<normalized_data>& output_queue) : 
            GenericWebSocket("Bybit", symbols, nsymbols, output_queue) {}
        std::vector<normalized_data> normalise_message(Json::Value message) override;
};

#endif // GENERICWS_HPP