#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <functional>
#include <unordered_map>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sodium.h>

namespace cell
{
    std::filesystem::path root = ".cell";
    namespace box
    {
        // check the tool call is allowed and the call is safe
        bool check(const std::string &call) {}
        bool grep(const std::string &path, const std::string &pattern, std::string &output) {}
        bool exec(const std::string &cmd, std::string &output) {}
        bool read(const std::string &path, std::string &output) {}
        bool write(const std::string &path, const std::string &input) {}
        bool exist(const std::string &path) {}
        bool remove(const std::string &path) {}
        bool mkdir(const std::string &dirpath) {}
    } // namespace box
    namespace net
    {
        void CURL_proxy(CURL *__handle__, const char *proxy, bool ssl = false)
        {
            curl_easy_setopt(__handle__, CURLOPT_PROXY, proxy);
            curl_easy_setopt(__handle__, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
            if (ssl)
                curl_easy_setopt(__handle__, CURLOPT_HTTPPROXYTUNNEL, 1L);
            curl_easy_setopt(__handle__, CURLOPT_PROXYUSERPWD, "user:password");
            curl_easy_setopt(__handle__, CURLOPT_PROXYAUTH, CURLAUTH_BASIC);
        }

        size_t CURL_WriteCallback(void *contents, size_t size, size_t nmemb, std::string &userp)
        {
            userp.append((char *)contents, size * nmemb);
            return size * nmemb;
        }

        bool CURL_get(CURL *__handle__, const char *URL, std::string &buf, std::string header = "")
        {
            if (!__handle__)
                return false;
            curl_easy_reset(__handle__);
            curl_easy_setopt(__handle__, CURLOPT_URL, URL);
            curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
            curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
            curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

            struct curl_slist *headers = nullptr;
            if (!header.empty())
            {
                headers = curl_slist_append(headers, header.c_str());
                curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
            }

            CURLcode res = curl_easy_perform(__handle__);
            if (headers)
                curl_slist_free_all(headers);
            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                return false;
            }
            return true;
        }

        bool CURL_get(CURL *__handle__, const char *URL, std::string &buf, const std::vector<std::string> &header_list)
        {
            if (!__handle__)
                return false;
            curl_easy_reset(__handle__);
            curl_easy_setopt(__handle__, CURLOPT_URL, URL);
            curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
            curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
            curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

            struct curl_slist *headers = nullptr;
            for (auto &header : header_list)
            {
                headers = curl_slist_append(headers, header.c_str());
                curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
            }

            CURLcode res = curl_easy_perform(__handle__);
            if (headers)
                curl_slist_free_all(headers);
            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                return false;
            }
            return true;
        }

        bool CURL_post(CURL *__handle__, const char *URL, const std::string &data, std::string &buf, std::string header = "")
        {
            if (!__handle__)
                return false;
            curl_easy_reset(__handle__);
            curl_easy_setopt(__handle__, CURLOPT_URL, URL);
            curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
            curl_easy_setopt(__handle__, CURLOPT_POST, 1L);
            curl_easy_setopt(__handle__, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(__handle__, CURLOPT_POSTFIELDSIZE, data.size());

            struct curl_slist *headers = nullptr;
            if (!header.empty())
            {
                headers = curl_slist_append(headers, header.c_str());
                curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
            }

            curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
            curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

            CURLcode res = curl_easy_perform(__handle__);
            if (headers)
                curl_slist_free_all(headers);
            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                return false;
            }
            return true;
        }

        bool CURL_post(CURL *__handle__, const char *URL, const std::string &data, std::string &buf, const std::vector<std::string> &header_list)
        {
            if (!__handle__)
                return false;
            curl_easy_reset(__handle__);
            curl_easy_setopt(__handle__, CURLOPT_URL, URL);
            curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
            curl_easy_setopt(__handle__, CURLOPT_POST, 1L);
            curl_easy_setopt(__handle__, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(__handle__, CURLOPT_POSTFIELDSIZE, data.size());

            struct curl_slist *headers = nullptr;
            for (auto &header : header_list)
            {
                headers = curl_slist_append(headers, header.c_str());
                curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
            }

            curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
            curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

            CURLcode res = curl_easy_perform(__handle__);
            if (headers)
                curl_slist_free_all(headers);
            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                return false;
            }
            return true;
        }

        using StreamCallback = std::function<void(const char *, size_t)>;
        size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
        {
            StreamCallback *callback = (StreamCallback *)userdata;
            try
            {
                (*callback)(ptr, size *nmemb);
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
            return size * nmemb;
        }

        bool CURL_stream_post(CURL *curl, const char *url, const std::string &post_data, const std::string &header, StreamCallback on_token)
        {
            if (!curl || !url)
                return false;

            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_token);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            struct curl_slist *header_list = nullptr;
            header_list = curl_slist_append(header_list, header.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

            CURLcode res = curl_easy_perform(curl);
            curl_slist_free_all(header_list);
            return (res == CURLE_OK);
        }

        bool CURL_stream_post(CURL *curl, const char *url, const std::string &post_data, const std::vector<std::string> &header_list, StreamCallback on_token)
        {
            if (!curl || !url)
                return false;

            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_token);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            struct curl_slist *headers = nullptr;
            for (auto &header : header_list)
                headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            CURLcode res = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            return (res == CURLE_OK);
        }
    } // namespace net
    namespace sys
    {
        class logger
        {
        private:
        public:
            logger() {}
            ~logger() {}
            void error(const std::string &msg) {}
            void info(const std::string &msg) {}
            void warn(const std::string &msg) {}
        };
    } // namespace sys
    namespace encrypt
    {
        std::filesystem::path credentials = root / ".crypt";

        class crypt
        {
        private:
            // crypt_map[key] = apikey_encrypted
            std::unordered_map<std::string, std::string> crypt_map;
            bool encrypt(const std::string &raw_value) {}
            std::string decrypt(const std::string &map_value) {}

        public:
            crypt() {}
            ~crypt() {}
            std::string get(const std::string &map_key)
            {
                if (crypt_map.contains(map_key))
                {
                    return decrypt(crypt_map[map_key]);
                }
            }
            bool add(const std::string &map_key, const std::string &raw_value)
            {
                if (crypt_map.contains(map_key))
                {
                    return false;
                }
                else
                {
                    return encrypt(raw_value);
                }
            }
            size_t remove(const std::string &map_key)
            {
                return crypt_map.erase(map_key);
            }
        };
    } // namespace encrypt
    namespace tools
    {
        enum class Policy : short
        {
            Deny = -1,
            Ask = 0,
            Allow = 1,
        };
        class tool
        {
        private:
            const size_t id = 0;
            const std::string key = "<null>";
            const Policy permission = Policy::Ask;

        public:
            tool(const size_t id, const std::string key, const Policy permission)
                : id(id), key(key), permission(permission) {}
            ~tool() {}
            virtual bool execute(const std::string &input, std::string &output) {}
        };
        std::pair<size_t, size_t> tools_call(const std::vector<std::pair<std::string, std::string>> &call_list, std::unordered_map<std::string, std::shared_ptr<tool>> &tool_list)
        {
            size_t succeed = 0, total = 0;
            for (auto &[call_tool, call_args] : call_list)
            {
                auto tool = tool_list.find(call_tool);
                if (tool != tool_list.end())
                {
                    std::string output;
                    if (tool->second->execute(call_args, output))
                    {
                        succeed += 1;
                    }
                    total += 1;
                }
            }
            return std::make_pair(succeed, total);
        }
    } // namespace tools
    namespace llm
    {
        class OpenAI
        {
        private:
            const std::string api_base;
            CURL *curl = curl_easy_init();

        public:
            OpenAI(const std::string &api_base) : api_base(api_base) {}
            ~OpenAI() {}
        };
        class Anthropic
        {
        private:
            const std::string api_base;
            CURL *curl = curl_easy_init();

        public:
            Anthropic(const std::string &api_base) : api_base(api_base) {}
            ~Anthropic() {}
        };
    } // namespace llm
    namespace chat
    {
        class session
        {
        private:
            const std::string session_id = "";
            nlohmann::json messages = nlohmann::json::array(); // [{"role":"user","content":"hi"},...]
            std::filesystem::path file;

        public:
            session() : session_id(std::to_string(std::time(nullptr)))
            {
                file = root / "sessions" / (session_id + ".json");
            }
            ~session() {}
            void load() {}
            void unload() {}
        };
        class history
        {
        private:
            std::unordered_map<std::string, session> session_list;

        public:
            history(/* args */) {}
            ~history() {}
            session &now() {}
            session &get() {}
            void remove(const std::string &session_id) {}
        };
    } // namespace chat
} // namespace cell

int main(int argc, char const *argv[])
{
    // args parse
    for (size_t i = 0; i < argc; i++)
    {
        /* code */
    }

    // agent loop
    try
    {
        while (true)
        {
            /* code */
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }

    return 0;
}