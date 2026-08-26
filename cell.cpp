#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <regex>
#include <utility>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <sstream>
#include <span>
#include <string_view>
#include <chrono>
#include <version>
#include <format>
#include <optional>
#include <concepts>
#if defined(__cpp_lib_generator)
#include <generator>
#endif
#if defined(__cpp_lib_expected)
#include <expected>
#endif
#include <source_location>
#include <exception>
#include <new>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sodium.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#ifdef __GNUG__
#include <cxxabi.h>
#endif

namespace cell
{
    std::filesystem::path root = ".cell";
    namespace box
    {
        // check the tool call is allowed and the call is safe
        bool check(std::string_view call)
        {
            if (call.find("..") != std::string_view::npos)
                return false;
            static constexpr std::string_view deny[] = {
                "rm -rf",
                "rmdir /s",
                "rd /s",
                "del /f /s",
                "format",
                "mkfs",
                "fdisk",
                "diskpart",
                "shutdown",
                "reg delete",
                "net user",
                "net localgroup",
                "taskkill",
                "powershell -enc",
                "powershell -e ",
                "cmd /c del",
                "cmd /c format",
                "dd if=",
                ":(){",
                "cacls",
                "icacls",
                "takeown",
                "attrib",
            };
            for (auto &p : deny)
            {
                if (call.find(p) != std::string_view::npos)
                    return false;
            }
            return true;
        }
        bool grep(std::string_view path, std::string_view pattern, std::string &output)
        {
            try
            {
                std::ifstream file{std::filesystem::path(path)};
                if (!file.is_open())
                    return false;
                std::regex re{std::string(pattern)};
                std::string line;
                while (std::getline(file, line))
                {
                    if (std::regex_search(line, re))
                        output += line + "\n";
                }
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }
        bool exec(std::string_view cmd, std::string &output)
        {
            if (!check(cmd))
                return false;
            std::string cmd_s(cmd);
#ifdef _WIN32
            FILE *pipe = _popen(cmd_s.c_str(), "r");
#else
            FILE *pipe = popen(cmd_s.c_str(), "r");
#endif
            if (!pipe)
                return false;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe))
                output += buf;
#ifdef _WIN32
            int rc = _pclose(pipe);
#else
            int rc = pclose(pipe);
#endif
            return rc == 0;
        }
        bool read(std::string_view path, std::string &output)
        {
            std::ifstream file(std::filesystem::path(path), std::ios::binary);
            if (!file.is_open())
                return false;
            file.seekg(0, std::ios::end);
            size_t size = (size_t)file.tellg();
            if (size > 1024 * 1024)
                return false;
            file.seekg(0);
            output.resize(size);
            file.read(&output[0], (std::streamsize)size);
            return file.good() || file.eof();
        }
        bool write(std::string_view path, std::string_view input)
        {
            std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
            if (!file.is_open())
                return false;
            file.write(input.data(), (std::streamsize)input.size());
            return file.good();
        }
        bool exist(std::string_view path)
        {
            std::error_code ec;
            return std::filesystem::exists(path, ec);
        }
        bool remove(std::string_view path)
        {
            std::error_code ec;
            return std::filesystem::remove(path, ec);
        }
        bool mkdir(std::string_view dirpath)
        {
            std::error_code ec;
            return std::filesystem::create_directories(dirpath, ec);
        }
        // edit a text file by line: op = query | insert | delete | replace, args = JSON string
        bool edit(std::string_view path, std::string_view op, std::string_view args, std::string &output)
        {
            std::vector<std::string> lines;
            bool trailing_nl = false;
            {
                std::string content;
                if (!read(path, content))
                    return false;
                trailing_nl = !content.empty() && content.back() == '\n';
                std::istringstream ss(content);
                std::string line;
                while (std::getline(ss, line))
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    lines.push_back(line);
                }
            }
            auto split_lines = [](const std::string &text) {
                std::vector<std::string> out;
                std::istringstream ss(text);
                std::string line;
                while (std::getline(ss, line))
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    out.push_back(line);
                }
                return out;
            };
            try
            {
                auto j = nlohmann::json::parse(args, nullptr, false);
                if (j.is_discarded())
                    return false;
                if (op == "query")
                {
                    std::regex re(j.value("pattern", ""));
                    for (size_t i = 0; i < lines.size(); i++)
                    {
                        if (std::regex_search(lines[i], re))
                            output += std::to_string(i + 1) + ": " + lines[i] + "\n";
                    }
                    return true;
                }
                else if (op == "insert")
                {
                    size_t line = (size_t)j.value("line", (size_t)1);
                    if (line == 0)
                        line = 1;
                    if (line > lines.size() + 1)
                        line = lines.size() + 1;
                    std::vector<std::string> add = split_lines(j.value("content", ""));
                    lines.insert(lines.begin() + (long long)(line - 1), add.begin(), add.end());
                    output = std::format("inserted {} line(s) before line {}", add.size(), line);
                }
                else if (op == "delete")
                {
                    size_t start = (size_t)j.value("start", (size_t)1);
                    size_t end = (size_t)j.value("end", start);
                    if (start < 1)
                        start = 1;
                    if (end < start)
                        end = start;
                    if (start > lines.size())
                    {
                        output = "nothing deleted (out of range)";
                        return true;
                    }
                    if (end > lines.size())
                        end = lines.size();
                    size_t n = end - start + 1;
                    lines.erase(lines.begin() + (long long)(start - 1), lines.begin() + (long long)end);
                    output = std::format("deleted {} line(s) from {} to {}", n, start, end);
                }
                else if (op == "replace")
                {
                    size_t count = 0;
                    if (j.contains("pattern") && j["pattern"].is_string())
                    {
                        std::regex re(j.value("pattern", ""));
                        std::string content = j.value("content", "");
                        for (auto &l : lines)
                        {
                            if (std::regex_search(l, re))
                            {
                                l = content;
                                count++;
                            }
                        }
                        output = std::format("replaced {} matching line(s)", count);
                        if (count == 0)
                            return true;
                    }
                    else
                    {
                        size_t start = (size_t)j.value("start", (size_t)1);
                        size_t end = (size_t)j.value("end", start);
                        if (start < 1)
                            start = 1;
                        if (end < start)
                            end = start;
                        if (start > lines.size())
                        {
                            output = "nothing replaced (out of range)";
                            return true;
                        }
                        if (end > lines.size())
                            end = lines.size();
                        std::vector<std::string> rep = split_lines(j.value("content", ""));
                        lines.erase(lines.begin() + (long long)(start - 1), lines.begin() + (long long)end);
                        lines.insert(lines.begin() + (long long)(start - 1), rep.begin(), rep.end());
                        output = std::format("replaced lines {}..{} with {} line(s)", start, end, rep.size());
                    }
                }
                else
                {
                    output = std::format("unknown op: {}", op);
                    return false;
                }
                std::string content;
                for (size_t i = 0; i < lines.size(); i++)
                {
                    content += lines[i];
                    if (i + 1 < lines.size() || trailing_nl)
                        content += "\n";
                }
                return write(path, content);
            }
            catch (const std::exception &)
            {
                return false;
            }
        }
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

        using StreamCallback = std::function<void(std::span<const char>)>;
        size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
        {
            StreamCallback *callback = (StreamCallback *)userdata;
            try
            {
                (*callback)(std::span<const char>(ptr, size * nmemb));
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
        namespace detail
        {
            inline bool color_force = true;
            inline bool color_enabled = false;

            void init_console()
            {
#ifdef _WIN32
                SetConsoleOutputCP(CP_UTF8);
                HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
                DWORD mode = 0;
                if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
                {
                    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
                    color_enabled = color_force;
                }
                else
                    color_enabled = false;
#else
                color_enabled = color_force && ::isatty(fileno(stdout)) != 0;
#endif
            }
        } // namespace detail

        enum class color : int
        {
            red = 31,
            green = 32,
            yellow = 33,
        };

        template <std::formattable<char>... Args>
        void print(std::format_string<Args...> fmt, Args &&...args)
        {
            std::string s = std::format(fmt, std::forward<Args>(args)...);
            std::fwrite(s.data(), 1, s.size(), stdout);
            std::fflush(stdout);
        }
        inline void print() {}
        template <std::formattable<char>... Args>
        void println(std::format_string<Args...> fmt, Args &&...args)
        {
            print(fmt, std::forward<Args>(args)...);
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }
        inline void println() { std::fputc('\n', stdout); std::fflush(stdout); }
        template <std::formattable<char>... Args>
        void eprintln(color c, std::format_string<Args...> fmt, Args &&...args)
        {
            std::string s = std::format(fmt, std::forward<Args>(args)...);
            if (detail::color_enabled)
                s = std::format("\x1b[{}m{}\x1b[0m", (int)c, s);
            s += '\n';
            std::fwrite(s.data(), 1, s.size(), stderr);
            std::fflush(stderr);
        }
        inline void eprintln(color) { std::fputc('\n', stderr); std::fflush(stderr); }
        template <std::formattable<char>... Args>
        void error(std::format_string<Args...> fmt, Args &&...args)
        {
            eprintln(color::red, fmt, std::forward<Args>(args)...);
        }
        template <std::formattable<char>... Args>
        void warn(std::format_string<Args...> fmt, Args &&...args)
        {
            eprintln(color::yellow, fmt, std::forward<Args>(args)...);
        }
        template <std::formattable<char>... Args>
        void info(std::format_string<Args...> fmt, Args &&...args)
        {
            eprintln(color::green, fmt, std::forward<Args>(args)...);
        }

        class logger
        {
        private:
            std::ofstream file;

            logger()
            {
                std::error_code ec;
                std::filesystem::create_directories(root / "logs", ec);
                file.open(root / "logs" / "cell.log", std::ios::app);
            }

            void write(std::string_view level, std::string_view msg, color c)
            {
                std::time_t t = std::time(nullptr);
                std::tm tm;
#ifdef _WIN32
                localtime_s(&tm, &t);
#else
                localtime_r(&t, &tm);
#endif
                char stamp[32];
                std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
                std::string line = std::format("[{}] [{}] {}", stamp, level, msg);
                if (file.is_open())
                {
                    file << line << '\n';
                    file.flush();
                }
                eprintln(c, "{}", line);
            }

        public:
            logger(const logger &) = delete;
            logger &operator=(const logger &) = delete;
            ~logger() {}
            static logger &instance()
            {
                static logger log;
                return log;
            }
            void close()
            {
                if (file.is_open())
                    file.close();
            }
            void error(std::string_view msg) { write("ERROR", msg, color::red); }
            void info(std::string_view msg) { write("INFO", msg, color::green); }
            void warn(std::string_view msg) { write("WARN", msg, color::yellow); }
        };

        class exception : public std::runtime_error
        {
        public:
            exception(std::string_view msg, std::source_location loc = std::source_location::current())
                : std::runtime_error(std::string(msg))
            {
                logger::instance().error(std::format("{} (at {}:{})", msg, loc.file_name(), loc.line()));
            }
        };

        [[noreturn]] inline void fatal(std::string_view msg, std::source_location loc = std::source_location::current())
        {
            throw exception(msg, loc);
        }
        inline void throw_if(bool cond, std::string_view msg, std::source_location loc = std::source_location::current())
        {
            if (cond)
                throw exception(msg, loc);
        }

        inline void install_handlers()
        {
            std::set_terminate([] {
                std::string type = "unknown";
#ifdef __GNUG__
                if (void *ex = __cxxabiv1::__cxa_current_exception_type(); ex)
                {
                    int status = 0;
                    char *demangled = abi::__cxa_demangle(((const std::type_info *)ex)->name(), nullptr, nullptr, &status);
                    type = demangled ? demangled : ((const std::type_info *)ex)->name();
                    std::free(demangled);
                }
#endif
                logger::instance().error(std::format("uncaught exception of type {}: terminate called", type));
                std::fflush(stderr);
                std::abort();
            });
            std::set_new_handler([] {
                logger::instance().error("out of memory (operator new failed)");
                std::fflush(stderr);
                std::abort();
            });
        }
    } // namespace sys
    namespace config
    {
        struct settings
        {
            std::string provider = "openai";
            std::string base;
            std::string model;
            std::string system_prompt = "You are a coding agent. Use the provided tools to read, search, write files and run commands when they help. When you finish a task, reply with a short summary of what was done.";
            std::string session_id;
        };
        std::filesystem::path file() { return root / "config.json"; }
#if defined(__cpp_lib_expected)
        using config_result = std::expected<settings, std::string>;
#else
        using config_result = std::optional<settings>;
#endif
        config_result load()
        {
            settings s;
            std::ifstream f(file());
            if (!f.is_open())
                return s;
            try
            {
                auto j = nlohmann::json::parse(f, nullptr, false);
                if (j.is_discarded())
                {
#if defined(__cpp_lib_expected)
                    return std::unexpected(std::string("config parse error"));
#else
                    return std::nullopt;
#endif
                }
                if (!j.is_object())
                {
#if defined(__cpp_lib_expected)
                    return std::unexpected(std::string("config is not an object"));
#else
                    return std::nullopt;
#endif
                }
                s.provider = j.value("provider", s.provider);
                s.base = j.value("base", s.base);
                s.model = j.value("model", s.model);
                s.system_prompt = j.value("system", s.system_prompt);
                s.session_id = j.value("session", s.session_id);
            }
            catch (const std::exception &e)
            {
#if defined(__cpp_lib_expected)
                return std::unexpected(std::format("config load failed: {}", e.what()));
#else
                cell::sys::warn("config load failed: {}", e.what());
                return std::nullopt;
#endif
            }
            return s;
        }
        bool save(const settings &s)
        {
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
            nlohmann::json j;
            j["provider"] = s.provider;
            j["base"] = s.base;
            j["model"] = s.model;
            j["system"] = s.system_prompt;
            j["session"] = s.session_id;
            std::ofstream f(file(), std::ios::trunc);
            if (!f.is_open())
                return false;
            f << j.dump(2);
            return f.good();
        }
    } // namespace config
    namespace encrypt
    {
        std::filesystem::path credentials() { return root / ".crypt"; }

        static constexpr std::string_view b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static std::string b64_encode(const std::string &in)
        {
            std::string out;
            int val = 0, bits = -6;
            for (unsigned char c : in)
            {
                val = (val << 8) + c;
                bits += 8;
                while (bits >= 0)
                {
                    out.push_back(b64_chars[(val >> bits) & 0x3F]);
                    bits -= 6;
                }
            }
            if (bits > -6)
                out.push_back(b64_chars[((val << 8) >> (bits + 8)) & 0x3F]);
            while (out.size() % 4)
                out.push_back('=');
            return out;
        }
        static std::string b64_decode(const std::string &in)
        {
            std::string out;
            int val = 0, bits = -8;
            for (unsigned char c : in)
            {
                if (c == '=' || c == '\n' || c == '\r')
                    break;
                size_t idx = b64_chars.find(c);
                if (idx == std::string::npos)
                    continue;
                val = (val << 6) + (int)idx;
                bits += 6;
                if (bits >= 0)
                {
                    out.push_back(char((val >> bits) & 0xFF));
                    bits -= 8;
                }
            }
            return out;
        }

        // in-memory secret: mlocked buffer, zeroized on destruction
        class secure_string
        {
        private:
            char *buf_ = nullptr;
            size_t len_ = 0;

            void release()
            {
                if (buf_)
                {
                    sodium_memzero(buf_, len_);
                    sodium_free(buf_);
                    buf_ = nullptr;
                    len_ = 0;
                }
            }
            void adopt(char *buf, size_t len)
            {
                release();
                buf_ = buf;
                len_ = len;
            }

        public:
            secure_string() = default;
            explicit secure_string(const std::string &s) { assign(s.data(), s.size()); }
            explicit secure_string(const char *s) { assign(s, s ? std::strlen(s) : 0); }
            secure_string(const secure_string &o) { assign(o.buf_, o.len_); }
            secure_string(secure_string &&o) noexcept : buf_(o.buf_), len_(o.len_)
            {
                o.buf_ = nullptr;
                o.len_ = 0;
            }
            secure_string &operator=(const secure_string &o)
            {
                if (this != &o)
                {
                    release();
                    assign(o.buf_, o.len_);
                }
                return *this;
            }
            secure_string &operator=(secure_string &&o) noexcept
            {
                if (this != &o)
                {
                    release();
                    buf_ = o.buf_;
                    len_ = o.len_;
                    o.buf_ = nullptr;
                    o.len_ = 0;
                }
                return *this;
            }
            ~secure_string() { release(); }

            void assign(const char *data, size_t len)
            {
                release();
                if (len)
                {
                    buf_ = (char *)sodium_malloc(len);
                    if (!buf_)
                        throw std::bad_alloc();
                    std::memcpy(buf_, data, len);
                    len_ = len;
                }
            }

            bool empty() const { return len_ == 0; }
            size_t size() const { return len_; }
            const char *data() const { return buf_ ? buf_ : ""; }
            const char *c_str() const { return data(); }
            std::string str() const { return std::string(data(), size()); }

            bool operator==(const secure_string &o) const
            {
                return len_ == o.len_ && (len_ == 0 || sodium_memcmp(buf_, o.buf_, len_) == 0);
            }
            bool operator==(const std::string &o) const
            {
                return len_ == o.size() && (len_ == 0 || sodium_memcmp(buf_, o.data(), len_) == 0);
            }
            bool operator==(const char *o) const
            {
                return o && len_ == std::strlen(o) && (len_ == 0 || sodium_memcmp(buf_, o, len_) == 0);
            }

            friend class crypt;
        };

        class crypt
        {
        private:
            // crypt_map[key] = apikey_encrypted
            std::unordered_map<std::string, std::string> crypt_map;
            std::filesystem::path key_file = root / ".key";

            bool load_key(unsigned char key[crypto_secretbox_KEYBYTES])
            {
                std::ifstream f(key_file);
                if (f.is_open())
                {
                    std::string raw = b64_decode(std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()));
                    if (raw.size() == crypto_secretbox_KEYBYTES)
                    {
                        std::memcpy(key, raw.data(), crypto_secretbox_KEYBYTES);
                        sodium_memzero(raw.data(), raw.size());
                        return true;
                    }
                }
                randombytes_buf(key, crypto_secretbox_KEYBYTES);
                std::error_code ec;
                std::filesystem::create_directories(root, ec);
                std::ofstream out(key_file, std::ios::trunc);
                if (!out.is_open())
                    return false;
                out << b64_encode(std::string((char *)key, crypto_secretbox_KEYBYTES));
                return true;
            }

            bool save()
            {
                std::error_code ec;
                std::filesystem::create_directories(root, ec);
                nlohmann::json j = nlohmann::json::object();
                for (auto &[k, v] : crypt_map)
                    j[k] = v;
                std::ofstream f(credentials(), std::ios::trunc);
                if (!f.is_open())
                    return false;
                f << j.dump(2);
                return f.good();
            }

            std::string encrypt(const char *data, size_t len)
            {
                unsigned char key[crypto_secretbox_KEYBYTES];
                if (!load_key(key))
                    return "";
                unsigned char nonce[crypto_secretbox_NONCEBYTES];
                randombytes_buf(nonce, sizeof nonce);
                std::string cipher;
                cipher.resize(len + crypto_secretbox_MACBYTES);
                if (crypto_secretbox_easy((unsigned char *)cipher.data(), (const unsigned char *)data, len, nonce, key) != 0)
                {
                    sodium_memzero(key, sizeof key);
                    return "";
                }
                sodium_memzero(key, sizeof key);
                std::string out((char *)nonce, sizeof nonce);
                out += cipher;
                return b64_encode(out);
            }
            secure_string decrypt(const std::string &map_value)
            {
                unsigned char key[crypto_secretbox_KEYBYTES];
                if (!load_key(key))
                    return secure_string();
                std::string raw = b64_decode(map_value);
                if (raw.size() <= crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
                {
                    sodium_memzero(key, sizeof key);
                    return secure_string();
                }
                const unsigned char *nonce = (const unsigned char *)raw.data();
                size_t clen = raw.size() - crypto_secretbox_NONCEBYTES;
                size_t plen = clen - crypto_secretbox_MACBYTES;
                unsigned char *plain = (unsigned char *)sodium_malloc(plen ? plen : 1);
                if (!plain)
                {
                    sodium_memzero(key, sizeof key);
                    return secure_string();
                }
                int rc = crypto_secretbox_open_easy(plain, (const unsigned char *)raw.data() + crypto_secretbox_NONCEBYTES, clen, nonce, key);
                sodium_memzero(key, sizeof key);
                if (rc != 0)
                {
                    sodium_free(plain);
                    return secure_string();
                }
                secure_string out;
                out.adopt((char *)plain, plen);
                return out;
            }

        public:
            crypt()
            {
                std::ifstream f(credentials());
                if (!f.is_open())
                    return;
                try
                {
                    auto j = nlohmann::json::parse(f, nullptr, false);
                    if (!j.is_object())
                        return;
                    for (auto &[k, v] : j.items())
                        crypt_map[k] = v.get<std::string>();
                }
                catch (const std::exception &)
                {
                }
            }
            ~crypt() {}
            secure_string get(const std::string &map_key)
            {
                if (crypt_map.find(map_key) != crypt_map.end())
                {
                    return decrypt(crypt_map[map_key]);
                }
                return secure_string();
            }
            bool add(const std::string &map_key, const std::string &raw_value)
            {
                if (crypt_map.find(map_key) != crypt_map.end())
                {
                    return false;
                }
                std::string enc = encrypt(raw_value.data(), raw_value.size());
                if (enc.empty())
                    return false;
                crypt_map[map_key] = enc;
                return save();
            }
            bool add(const std::string &map_key, const secure_string &raw_value)
            {
                if (crypt_map.find(map_key) != crypt_map.end())
                {
                    return false;
                }
                std::string enc = encrypt(raw_value.data(), raw_value.size());
                if (enc.empty())
                    return false;
                crypt_map[map_key] = enc;
                return save();
            }
            size_t remove(const std::string &map_key)
            {
                size_t n = crypt_map.erase(map_key);
                if (n)
                    save();
                return n;
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
            virtual bool execute(const std::string &input, std::string &output)
            {
                (void)input;
                (void)output;
                return false;
            }
            size_t tool_id() const { return id; }
            const std::string &name() const { return key; }
            Policy policy() const { return permission; }
        };
        template <typename F>
        concept tool_handler = requires(F f, const std::string &input, std::string &output)
        {
            { f(input, output) } -> std::convertible_to<bool>;
        };

        class callable_tool : public tool
        {
        private:
            using handler_t = std::function<bool(const std::string &input, std::string &output)>;
            handler_t handler_;

        public:
            template <tool_handler F>
            callable_tool(size_t id, const std::string &key, Policy permission, F &&fn)
                : tool(id, key, permission), handler_(std::forward<F>(fn)) {}
            ~callable_tool() {}
            bool execute(const std::string &input, std::string &output) override
            {
                if (policy() == Policy::Deny)
                    return false;
                if (policy() == Policy::Ask)
                {
                    std::cout << "allow " << name() << "(" << input << ")? [y/N] " << std::flush;
                    std::string answer;
                    std::getline(std::cin, answer);
                    if (answer != "y" && answer != "Y")
                        return false;
                }
                if (!box::check(input))
                    return false;
                return handler_(input, output);
            }
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
        // consumes the next complete "data:" line from buf, extracts its payload
        static inline bool sse_next_line(std::string &buf, std::string &payload)
        {
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos)
            {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (line.empty() || line == "\r")
                    continue;
                if (line.rfind("data:", 0) != 0)
                    continue;
                payload = line.substr(5);
                while (!payload.empty() && (payload.front() == ' ' || payload.front() == '\r'))
                    payload.erase(payload.begin());
                if (payload.empty() || payload == "[DONE]")
                    continue;
                return true;
            }
            return false;
        }
#if defined(__cpp_lib_generator)
        // lazy coroutine SSE parser: yields parsed JSON events
        static std::generator<nlohmann::json> sse_events(std::string &buf)
        {
            std::string payload;
            while (sse_next_line(buf, payload))
            {
                try
                {
                    co_yield nlohmann::json::parse(payload);
                }
                catch (const std::exception &)
                {
                }
            }
        }
#else
        // fallback SSE parser for toolchains without std::generator
        static inline bool sse_next(std::string &buf, nlohmann::json &out)
        {
            std::string payload;
            if (!sse_next_line(buf, payload))
                return false;
            try
            {
                out = nlohmann::json::parse(payload);
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }
#endif

        class OpenAI
        {
        private:
            const std::string api_base;
            CURL *curl = curl_easy_init();

            static std::vector<std::string> headers(const encrypt::secure_string &api_key)
            {
                std::string auth = "Authorization: Bearer ";
                auth.append(api_key.data(), api_key.size());
                return {"Content-Type: application/json", std::move(auth)};
            }
            static nlohmann::json body(const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, bool stream)
            {
                nlohmann::json b;
                b["model"] = model;
                b["messages"] = messages;
                if (tools.is_array() && !tools.empty())
                    b["tools"] = tools;
                b["stream"] = stream;
                return b;
            }

        public:
            OpenAI(const std::string &api_base) : api_base(api_base) {}
            ~OpenAI() { curl_easy_cleanup(curl); }

            bool chat(const encrypt::secure_string &api_key, const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, nlohmann::json &reply, nlohmann::json &tool_calls)
            {
                tool_calls = nlohmann::json::array();
                std::string buf;
                std::string url = api_base + "/chat/completions";
                std::vector<std::string> hdrs = headers(api_key);
                bool ok = net::CURL_post(curl, url.c_str(), body(model, messages, tools, false).dump(), buf, hdrs);
                for (auto &h : hdrs)
                    sodium_memzero(h.data(), h.size());
                if (!ok)
                    return false;
                try
                {
                    auto j = nlohmann::json::parse(buf);
                    if (!j.contains("choices") || j["choices"].empty())
                        return false;
                    reply = j["choices"][0]["message"];
                    if (reply.contains("tool_calls"))
                        tool_calls = reply["tool_calls"];
                    return true;
                }
                catch (const std::exception &e)
                {
                    std::cerr << "openai parse error: " << e.what() << std::endl;
                    return false;
                }
            }

            bool chat_stream(const encrypt::secure_string &api_key, const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, net::StreamCallback on_token, nlohmann::json &reply, nlohmann::json &tool_calls)
            {
                tool_calls = nlohmann::json::array();
                std::string text;
                std::string sse_buf;
                std::string url = api_base + "/chat/completions";
                net::StreamCallback cb = [&](std::span<const char> data)
                {
                    sse_buf.append(data.data(), data.size());
                    auto handle = [&](const nlohmann::json &j)
                    {
                        if (!j.contains("choices") || j["choices"].empty())
                            return;
                        auto &delta = j["choices"][0]["delta"];
                        if (delta.contains("content") && delta["content"].is_string())
                        [[likely]]
                        {
                            std::string t = delta["content"].get<std::string>();
                            text += t;
                            on_token(std::span<const char>(t));
                        }
                        if (delta.contains("tool_calls") && delta["tool_calls"].is_array())
                        {
                            for (auto &tc : delta["tool_calls"])
                            {
                                size_t idx = tc.value("index", (size_t)tool_calls.size());
                                while (tool_calls.size() <= idx)
                                    tool_calls.push_back({{"id", ""}, {"type", "function"}, {"function", {{"name", ""}, {"arguments", ""}}}});
                                auto &acc = tool_calls[idx];
                                if (tc.contains("id") && tc["id"].is_string())
                                    acc["id"] = tc["id"];
                                if (tc.contains("function"))
                                {
                                    if (tc["function"].contains("name") && tc["function"]["name"].is_string())
                                        acc["function"]["name"] = tc["function"]["name"];
                                    if (tc["function"].contains("arguments") && tc["function"]["arguments"].is_string())
                                        acc["function"]["arguments"] = acc["function"]["arguments"].get<std::string>() + tc["function"]["arguments"].get<std::string>();
                                }
                            }
                        }
                    };
#if defined(__cpp_lib_generator)
                    for (const nlohmann::json &j : sse_events(sse_buf))
                        handle(j);
#else
                    nlohmann::json j;
                    while (sse_next(sse_buf, j))
                        handle(j);
#endif
                };
                std::vector<std::string> hdrs = headers(api_key);
                bool ok = net::CURL_stream_post(curl, url.c_str(), body(model, messages, tools, true).dump(), hdrs, cb);
                for (auto &h : hdrs)
                    sodium_memzero(h.data(), h.size());
                if (!ok)
                    return false;
                reply["role"] = "assistant";
                if (text.empty())
                    reply["content"] = nullptr;
                else
                    reply["content"] = text;
                if (!tool_calls.empty())
                    reply["tool_calls"] = tool_calls;
                return true;
            }
        };
        class Anthropic
        {
        private:
            const std::string api_base;
            CURL *curl = curl_easy_init();

            static std::vector<std::string> headers(const encrypt::secure_string &api_key)
            {
                std::string auth = "x-api-key: ";
                auth.append(api_key.data(), api_key.size());
                return {"Content-Type: application/json", std::move(auth), "anthropic-version: 2023-06-01"};
            }
            static nlohmann::json body(const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, bool stream)
            {
                nlohmann::json b;
                b["model"] = model;
                b["max_tokens"] = 4096;
                nlohmann::json sys = nlohmann::json::array();
                nlohmann::json msgs = nlohmann::json::array();
                for (auto &m : messages)
                {
                    if (m.value("role", "") == "system")
                        sys.push_back(m.value("content", ""));
                    else
                        msgs.push_back(m);
                }
                if (!sys.empty())
                    b["system"] = sys;
                b["messages"] = msgs;
                if (tools.is_array() && !tools.empty())
                    b["tools"] = tools;
                b["stream"] = stream;
                return b;
            }

        public:
            Anthropic(const std::string &api_base) : api_base(api_base) {}
            ~Anthropic() { curl_easy_cleanup(curl); }

            bool chat(const encrypt::secure_string &api_key, const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, nlohmann::json &reply, nlohmann::json &tool_calls)
            {
                tool_calls = nlohmann::json::array();
                std::string buf;
                std::string url = api_base + "/v1/messages";
                std::vector<std::string> hdrs = headers(api_key);
                bool ok = net::CURL_post(curl, url.c_str(), body(model, messages, tools, false).dump(), buf, hdrs);
                for (auto &h : hdrs)
                    sodium_memzero(h.data(), h.size());
                if (!ok)
                    return false;
                try
                {
                    auto j = nlohmann::json::parse(buf);
                    if (!j.contains("content") || !j["content"].is_array())
                        return false;
                    reply["role"] = "assistant";
                    reply["content"] = j["content"];
                    for (auto &block : j["content"])
                    {
                        if (block.value("type", "") != "tool_use")
                            continue;
                        nlohmann::json tc;
                        tc["id"] = block.value("id", "");
                        tc["type"] = "function";
                        tc["function"]["name"] = block.value("name", "");
                        tc["function"]["arguments"] = block.contains("input") ? block["input"].dump() : "{}";
                        tool_calls.push_back(tc);
                    }
                    return true;
                }
                catch (const std::exception &)
                {
                    return false;
                }
            }

            bool chat_stream(const encrypt::secure_string &api_key, const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, net::StreamCallback on_token, nlohmann::json &reply, nlohmann::json &tool_calls)
            {
                tool_calls = nlohmann::json::array();
                std::vector<nlohmann::json> blocks;
                std::string sse_buf;
                std::string url = api_base + "/v1/messages";
                net::StreamCallback cb = [&](std::span<const char> data)
                {
                    sse_buf.append(data.data(), data.size());
                    auto handle = [&](const nlohmann::json &ev)
                    {
                        std::string type = ev.value("type", "");
                        size_t idx = ev.value("index", (size_t)blocks.size());
                        if (type == "content_block_start")
                        {
                            while (blocks.size() <= idx)
                                blocks.push_back(nlohmann::json());
                            blocks[idx] = ev.value("content_block", nlohmann::json());
                        }
                        else if (type == "content_block_delta" && idx < blocks.size())
                        [[likely]]
                        {
                            auto &delta = ev["delta"];
                            std::string dt = delta.value("type", "");
                            if (dt == "text_delta" && blocks[idx].value("type", "") == "text")
                            {
                                std::string t = delta.value("text", "");
                                blocks[idx]["text"] = blocks[idx].value("text", "") + t;
                                on_token(std::span<const char>(t));
                            }
                            else if (dt == "input_json_delta" && blocks[idx].value("type", "") == "tool_use")
                                blocks[idx]["input"] = blocks[idx].value("input", "") + delta.value("partial_json", "");
                        }
                    };
#if defined(__cpp_lib_generator)
                    for (const nlohmann::json &ev : sse_events(sse_buf))
                        handle(ev);
#else
                    nlohmann::json ev;
                    while (sse_next(sse_buf, ev))
                        handle(ev);
#endif
                };
                std::vector<std::string> hdrs = headers(api_key);
                bool ok = net::CURL_stream_post(curl, url.c_str(), body(model, messages, tools, true).dump(), hdrs, cb);
                for (auto &h : hdrs)
                    sodium_memzero(h.data(), h.size());
                if (!ok)
                    return false;
                reply["role"] = "assistant";
                reply["content"] = blocks.empty() ? nlohmann::json::array() : nlohmann::json(blocks);
                for (auto &block : blocks)
                {
                    if (block.value("type", "") != "tool_use")
                        continue;
                    nlohmann::json tc;
                    tc["id"] = block.value("id", "");
                    tc["type"] = "function";
                    tc["function"]["name"] = block.value("name", "");
                    std::string args = block.value("input", "{}");
                    try
                    {
                        auto parsed = nlohmann::json::parse(args);
                        args = parsed.dump();
                    }
                    catch (const std::exception &)
                    {
                    }
                    tc["function"]["arguments"] = args;
                    tool_calls.push_back(tc);
                }
                return true;
            }
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
            bool loaded = false;

        public:
            session() : session_id(std::format("{}", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()))
            {
                file = root / "sessions" / (session_id + ".json");
            }
            session(const std::string &id) : session_id(id)
            {
                file = root / "sessions" / (session_id + ".json");
            }
            ~session() {}
            void load()
            {
                if (loaded)
                    return;
                loaded = true;
                if (!std::filesystem::exists(file))
                    return;
                std::ifstream f(file);
                try
                {
                    auto j = nlohmann::json::parse(f, nullptr, false);
                    if (j.contains("messages") && j["messages"].is_array())
                        messages = j["messages"];
                }
                catch (const std::exception &)
                {
                    messages = nlohmann::json::array();
                }
            }
            void unload()
            {
                std::error_code ec;
                std::filesystem::create_directories(root / "sessions", ec);
                nlohmann::json j;
                j["id"] = session_id;
                j["messages"] = messages;
                std::ofstream f(file, std::ios::trunc);
                if (f.is_open())
                    f << j.dump(2);
            }
            const std::string &id() const { return session_id; }
            nlohmann::json &msg() { return messages; }
            void append(const std::string &role, const nlohmann::json &content)
            {
                messages.push_back({{"role", role}, {"content", content}});
            }
        };
        class history
        {
        private:
            std::unordered_map<std::string, session> session_list;
            std::string current = "current";

        public:
            history(/* args */) {}
            ~history() {}
            session &now()
            {
                auto it = session_list.find(current);
                if (it == session_list.end())
                {
                    session s = (current == "current") ? session() : session(current);
                    it = session_list.emplace(current, std::move(s)).first;
                }
                it->second.load();
                return it->second;
            }
            session &get()
            {
                auto it = session_list.find(current);
                if (it == session_list.end())
                    throw std::runtime_error("no active session");
                return it->second;
            }
            void use(const std::string &session_id) { current = session_id; }
            void remove(const std::string &session_id)
            {
                for (auto it = session_list.begin(); it != session_list.end(); ++it)
                {
                    if (it->second.id() != session_id)
                        continue;
                    std::string key = it->first;
                    std::error_code ec;
                    std::filesystem::remove(root / "sessions" / (session_id + ".json"), ec);
                    session_list.erase(it);
                    if (current == key)
                        current = "current";
                    break;
                }
            }
        };
    } // namespace chat
} // namespace cell

static void print_usage(const char *prog)
{
    cell::sys::println("usage: {} [options]", prog);
    cell::sys::println("  --provider openai|anthropic  llm provider (default: openai)");
    cell::sys::println("  --base URL                   api base url");
    cell::sys::println("  --model MODEL                model name");
    cell::sys::println("  --key KEY                    api key (saved to the encrypted vault)");
    cell::sys::println("  --session ID                 resume an existing session");
    cell::sys::println("  --system TEXT                system prompt");
    cell::sys::println("  --no-color                   disable colored log output");
    cell::sys::println("  --selftest                   run internal self tests");
}

static std::pair<std::unordered_map<std::string, std::shared_ptr<cell::tools::tool>>, nlohmann::json> build_tools(bool anthropic)
{
    using cell::tools::Policy;
    std::unordered_map<std::string, std::shared_ptr<cell::tools::tool>> list;
    nlohmann::json defs = nlohmann::json::array();
    size_t id = 0;
    auto str_prop = [](const std::string &desc)
    { return nlohmann::json{{"type", "string"}, {"description", desc}}; };
    auto add = [&](const std::string &name, const std::string &desc, const nlohmann::json &props,
                   const std::vector<std::string> &required, Policy policy,
                   std::function<bool(const std::string &, std::string &)> fn)
    {
        list[name] = std::make_shared<cell::tools::callable_tool>(id++, name, policy, std::move(fn));
        nlohmann::json schema = {{"type", "object"}, {"properties", props}, {"required", required}};
        if (anthropic)
            defs.push_back({{"name", name}, {"description", desc}, {"input_schema", schema}});
        else
            defs.push_back({{"type", "function"}, {"function", {{"name", name}, {"description", desc}, {"parameters", schema}}}});
    };

    add("exec", "Run a shell command and return its stdout output.", {{"cmd", str_prop("shell command to execute")}}, {"cmd"}, Policy::Ask,
        [](const std::string &in, std::string &out)
        {
            try
            {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_discarded())
                    return false;
                return cell::box::exec(j.value("cmd", ""), out);
            }
            catch (const std::exception &)
            {
                return false;
            }
        });
    add("grep", "Search a text file line by line for matches of a regex pattern.", {{"path", str_prop("file path")}, {"pattern", str_prop("regex pattern")}}, {"path", "pattern"}, Policy::Allow,
        [](const std::string &in, std::string &out)
        {
            try
            {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_discarded())
                    return false;
                return cell::box::grep(j.value("path", ""), j.value("pattern", ""), out);
            }
            catch (const std::exception &)
            {
                return false;
            }
        });
    add("read", "Read a text file (max 1MB) and return its contents.", {{"path", str_prop("file path")}}, {"path"}, Policy::Allow,
        [](const std::string &in, std::string &out)
        {
            try
            {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_discarded())
                    return false;
                return cell::box::read(j.value("path", ""), out);
            }
            catch (const std::exception &)
            {
                return false;
            }
        });
    add("write", "Write text content to a file, overwriting any existing content.", {{"path", str_prop("file path")}, {"content", str_prop("text content")}}, {"path", "content"}, Policy::Ask,
        [](const std::string &in, std::string &out)
        {
            (void)out;
            try
            {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_discarded())
                    return false;
                return cell::box::write(j.value("path", ""), j.value("content", ""));
            }
            catch (const std::exception &)
            {
                return false;
            }
        });
    add("exist", "Check whether a file or directory exists.", {{"path", str_prop("file or directory path")}}, {"path"}, Policy::Allow,
        [](const std::string &in, std::string &out)
        {
            (void)out;
            try
            {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_discarded())
                    return false;
                out = cell::box::exist(j.value("path", "")) ? "yes" : "no";
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        });
    add("remove", "Delete a file or an empty directory.", {{"path", str_prop("file or directory path")}}, {"path"}, Policy::Ask,
        [](const std::string &in, std::string &out)
        {
            (void)out;
            try
            {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_discarded())
                    return false;
                return cell::box::remove(j.value("path", ""));
            }
            catch (const std::exception &)
            {
                return false;
            }
        });
    add("mkdir", "Create a directory, including any missing parents.", {{"dirpath", str_prop("directory path")}}, {"dirpath"}, Policy::Ask,
        [](const std::string &in, std::string &out)
        {
            (void)out;
            try
            {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_discarded())
                    return false;
                return cell::box::mkdir(j.value("dirpath", ""));
            }
            catch (const std::exception &)
            {
                return false;
            }
        });
    add("edit", "Edit a text file by line. op \"query\": {\"pattern\":\"regex\"} returns matching lines with line numbers; op \"insert\": {\"line\":N,\"content\":\"...\"} inserts content before line N (N<=1 means top, N>len appends at end); op \"delete\": {\"start\":A,\"end\":B} removes lines A..B; op \"replace\": {\"start\":A,\"end\":B,\"content\":\"...\"} replaces the line range, or {\"pattern\":\"regex\",\"content\":\"...\"} replaces every matching line.", {{"path", str_prop("file path")}, {"op", str_prop("query|insert|delete|replace")}, {"args", str_prop("JSON arguments for the operation")}}, {"path", "op", "args"}, Policy::Ask,
        [](const std::string &in, std::string &out)
        {
            try
            {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_discarded())
                    return false;
                return cell::box::edit(j.value("path", ""), j.value("op", ""), j.value("args", ""), out);
            }
            catch (const std::exception &)
            {
                return false;
            }
        });
    return {list, defs};
}

static int run_selftest()
{
    bool ok = true;
    std::filesystem::path saved_root = cell::root;
    cell::root = ".cell-selftest";
    std::error_code ec;
    std::filesystem::remove_all(cell::root, ec);
    int sodium_rc = sodium_init(); (void)sodium_rc;
    auto expect = [&ok](bool cond, const char *what)
    {
        if (!cond)
            std::cerr << "FAIL: " << what << std::endl;
        ok = ok && cond;
    };

    std::string out;
    expect(!cell::box::check("rm -rf /"), "box::check rejects rm -rf");
    expect(!cell::box::check("cat ../etc/passwd"), "box::check rejects path traversal");
    expect(cell::box::check("echo hi"), "box::check allows echo");
    expect(cell::box::write("box_test.txt", "hello\nworld\n"), "box::write");
    expect(cell::box::read("box_test.txt", out) && out == "hello\nworld\n", "box::read");
    expect(cell::box::grep("box_test.txt", "wor", out) && out.find("world") != std::string::npos, "box::grep");
    expect(cell::box::exist("box_test.txt"), "box::exist");
    expect(cell::box::mkdir("box_dir/sub"), "box::mkdir");
    expect(cell::box::exist("box_dir/sub"), "box::mkdir created");
    std::string cmd_out;
    expect(cell::box::exec("echo cell_selftest", cmd_out) && cmd_out.find("cell_selftest") != std::string::npos, "box::exec");
    expect(cell::box::remove("box_test.txt") && !cell::box::exist("box_test.txt"), "box::remove file");
    expect(cell::box::remove("box_dir/sub") && cell::box::remove("box_dir"), "box::remove dir");

    std::string edit_out;
    expect(cell::box::write("edit_test.txt", "aaa\nbbb\nccc\n"), "edit fixture");
    expect(cell::box::edit("edit_test.txt", "query", "{\"pattern\":\"b\"}", edit_out) && edit_out == "2: bbb\n", "box::edit query");
    expect(cell::box::edit("edit_test.txt", "insert", "{\"line\":2,\"content\":\"XYZ\"}", edit_out) && edit_out.find("inserted 1 line") != std::string::npos, "box::edit insert");
    expect(cell::box::read("edit_test.txt", out) && out == "aaa\nXYZ\nbbb\nccc\n", "box::edit insert result");
    expect(cell::box::edit("edit_test.txt", "replace", "{\"pattern\":\"b\",\"content\":\"BBB\"}", edit_out) && edit_out.find("replaced 1") != std::string::npos, "box::edit replace pattern");
    expect(cell::box::read("edit_test.txt", out) && out == "aaa\nXYZ\nBBB\nccc\n", "box::edit replace pattern result");
    expect(cell::box::edit("edit_test.txt", "replace", "{\"start\":1,\"end\":2,\"content\":\"TOP\\nSUB\"}", edit_out) && edit_out.find("replaced lines 1..2") != std::string::npos, "box::edit replace range");
    expect(cell::box::read("edit_test.txt", out) && out == "TOP\nSUB\nBBB\nccc\n", "box::edit replace range result");
    expect(cell::box::edit("edit_test.txt", "delete", "{\"start\":3,\"end\":4}", edit_out) && edit_out.find("deleted 2 line") != std::string::npos, "box::edit delete");
    expect(cell::box::read("edit_test.txt", out) && out == "TOP\nSUB\n", "box::edit delete result");
    expect(cell::box::remove("edit_test.txt"), "edit cleanup");

    auto &log = cell::sys::logger::instance();
    log.info("selftest info");
    log.warn("selftest warn");
    log.error("selftest error");
    expect(cell::box::exist((cell::root / "logs" / "cell.log").string()), "logger writes log file");

    bool threw = false;
    try
    {
        cell::sys::fatal("selftest exception");
    }
    catch (const cell::sys::exception &e)
    {
        threw = std::string(e.what()).find("selftest exception") != std::string::npos;
    }
    expect(threw, "sys::exception thrown and caught");
    expect(cell::box::read((cell::root / "logs" / "cell.log").string(), out) && out.find("selftest exception") != std::string::npos, "sys::exception logged");
    bool no_throw = true;
    try
    {
        cell::sys::throw_if(true, "should throw");
        no_throw = false;
    }
    catch (const cell::sys::exception &)
    {
    }
    expect(no_throw, "sys::throw_if");

    cell::encrypt::crypt vault;
    expect(vault.add("selftest_key", "secret-123"), "crypt::add");
    cell::encrypt::secure_string k1 = vault.get("selftest_key");
    expect(k1 == "secret-123" && k1.size() == 10, "crypt::get roundtrip");
    expect(!vault.add("selftest_key", "other"), "crypt::add duplicate rejected");
    expect(vault.remove("selftest_key") == 1, "crypt::remove");
    expect(vault.get("selftest_key").empty(), "crypt::get after remove");
    expect(vault.add("persist_key", "keep-me") && vault.get("persist_key") == "keep-me", "crypt::persist write");
    cell::encrypt::crypt reloaded;
    expect(reloaded.get("persist_key") == "keep-me", "crypt reloads from disk");
    std::string vault_file;
    expect(cell::box::read((cell::root / ".crypt").string(), vault_file), "read vault file");
    expect(vault_file.find("keep-me") == std::string::npos, "vault stores ciphertext only");

    cell::config::settings cfg;
    cfg.provider = "anthropic";
    cfg.model = "test-model";
    expect(cell::config::save(cfg), "config::save");
    auto cfg_res = cell::config::load();
    expect(cfg_res.has_value() && cfg_res->provider == "anthropic" && cfg_res->model == "test-model", "config::load roundtrip");
    cell::box::write((cell::root / "config.json").string(), "{invalid");
    expect(!cell::config::load().has_value(), "config::load reports parse error");

    cell::sys::logger::instance().close();
    std::filesystem::remove_all(cell::root, ec);
    cell::root = saved_root;
    cell::sys::println("selftest {}", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}

int main(int argc, char const *argv[])
{
    // args parse
    bool selftest = false;
    std::string key_arg;
    cell::config::settings cfg;
    if (auto res = cell::config::load(); res)
        cfg = *res;
#if defined(__cpp_lib_expected)
    else
        cell::sys::warn("{}", res.error());
#endif
    for (size_t i = 1; i < (size_t)argc; i++)
    {
        std::string arg = argv[i];
        auto value = [&](const char *name) -> std::string
        {
            if (i + 1 >= (size_t)argc)
            {
                cell::sys::error("missing value for {}", name);
                std::exit(1);
            }
            return argv[++i];
        };
        if (arg == "--provider")
            cfg.provider = value("--provider");
        else if (arg == "--base")
            cfg.base = value("--base");
        else if (arg == "--model")
            cfg.model = value("--model");
        else if (arg == "--key")
            key_arg = value("--key");
        else if (arg == "--session")
            cfg.session_id = value("--session");
        else if (arg == "--system")
            cfg.system_prompt = value("--system");
        else if (arg == "--no-color")
            cell::sys::detail::color_force = false;
        else if (arg == "--selftest")
            selftest = true;
        else
        {
            cell::sys::error("unknown argument: {}", arg);
            print_usage(argv[0]);
            return 1;
        }
    }
    cell::sys::detail::init_console();
    cell::sys::install_handlers();
    if (selftest)
        return run_selftest();

    if (cfg.provider != "openai" && cfg.provider != "anthropic")
    {
        cell::sys::error("unsupported provider: {}", cfg.provider);
        return 1;
    }
    if (cfg.base.empty())
        cfg.base = (cfg.provider == "openai") ? "https://api.openai.com/v1" : "https://api.anthropic.com";
    if (cfg.model.empty())
        cfg.model = (cfg.provider == "openai") ? "gpt-4o-mini" : "claude-3-5-haiku-latest";

    curl_global_init(CURL_GLOBAL_DEFAULT);
    int sodium_rc = sodium_init(); (void)sodium_rc;

    auto &log = cell::sys::logger::instance();
    log.info(std::format("cell started, provider={}, model={}", cfg.provider, cfg.model));

    cell::encrypt::crypt vault;
    cell::encrypt::secure_string api_key;
    if (!key_arg.empty())
    {
        api_key = cell::encrypt::secure_string(key_arg);
        vault.add("api_key", api_key);
        sodium_memzero(key_arg.data(), key_arg.size());
    }
    else
    {
        const char *env = std::getenv(cfg.provider == "openai" ? "OPENAI_API_KEY" : "ANTHROPIC_API_KEY");
        if (env && *env)
            api_key = cell::encrypt::secure_string(env);
        else
            api_key = vault.get("api_key");
    }
    if (api_key.empty())
    {
        cell::sys::error("no api key: set the {} environment variable, or store one in the vault (.cell/.crypt)",
                         cfg.provider == "openai" ? "OPENAI_API_KEY" : "ANTHROPIC_API_KEY");
        return 1;
    }

    auto [tool_list, tool_defs] = build_tools(cfg.provider == "anthropic");

    std::unique_ptr<cell::llm::OpenAI> openai;
    std::unique_ptr<cell::llm::Anthropic> anthropic;
    if (cfg.provider == "openai")
        openai = std::make_unique<cell::llm::OpenAI>(cfg.base);
    else
        anthropic = std::make_unique<cell::llm::Anthropic>(cfg.base);

    cell::chat::history h;
    if (!cfg.session_id.empty())
        h.use(cfg.session_id);
    cell::chat::session *s = &h.now();
    if (s->msg().empty())
        s->msg().push_back({{"role", "system"}, {"content", cfg.system_prompt}});
    cell::sys::println("cell: session={} provider={} model={}", s->id(), cfg.provider, cfg.model);

    bool interactive = true;
#ifdef _WIN32
    interactive = _isatty(_fileno(stdin)) != 0;
#else
    interactive = ::isatty(fileno(stdin)) != 0;
#endif
    std::string pending;
    if (!interactive)
    {
        std::string line;
        while (std::getline(std::cin, line))
            pending += line + "\n";
    }

    auto on_token = [](std::span<const char> data)
    {
        std::fwrite(data.data(), 1, data.size(), stdout);
        std::fflush(stdout);
    };

    // agent loop
    try
    {
        bool running = true;
        while (running)
        {
            std::string input;
            if (interactive)
            {
                cell::sys::print("> ");
                if (!std::getline(std::cin, input))
                    break;
            }
            else
            {
                input = pending;
                pending.clear();
                if (input.empty())
                    break;
            }
            if (input.empty())
                continue;
            if (input == "/exit" || input == "/quit")
                break;
            if (input == "/save")
            {
                s->unload();
                cell::sys::println("session saved: {}", s->id());
                continue;
            }
            if (input == "/new")
            {
                std::string old_id = s->id();
                h.remove(old_id);
                s = &h.now();
                s->msg().push_back({{"role", "system"}, {"content", cfg.system_prompt}});
                cell::sys::println("new session: {}", s->id());
                continue;
            }

            s->msg().push_back({{"role", "user"}, {"content", input}});
            log.info(std::format("user: {}", input));
            cell::sys::print("reply> ");

            bool done = false;
            int rounds = 0;
            while (!done && rounds++ < 8)
            {
                nlohmann::json reply;
                nlohmann::json tool_calls = nlohmann::json::array();
                bool ok = false;
                if (cfg.provider == "openai")
                    ok = openai->chat_stream(api_key, cfg.model, s->msg(), tool_defs, on_token, reply, tool_calls);
                else
                    ok = anthropic->chat_stream(api_key, cfg.model, s->msg(), tool_defs, on_token, reply, tool_calls);
                if (!ok)
                {
                    log.error("llm request failed");
                    cell::sys::println();
                    cell::sys::error("[llm request failed]");
                    break;
                }
                cell::sys::println();
                s->msg().push_back(reply);
                if (!tool_calls.empty())
                {
                    for (auto &tc : tool_calls)
                    {
                        std::string name = tc["function"].value("name", "");
                        std::string args = tc["function"].value("arguments", "");
                        std::string output = "[tool failed]";
                        auto it = tool_list.find(name);
                        if (it == tool_list.end())
                            output = std::format("[unknown tool: {}]", name);
                        else
                        {
                            std::string o;
                            if (it->second->execute(args, o))
                                output = o;
                            else
                                output = "[tool denied or failed]";
                        }
                        log.info(std::format("tool {} -> {}", name, output.substr(0, 200)));
                        if (cfg.provider == "openai")
                            s->msg().push_back({{"role", "tool"}, {"tool_call_id", tc.value("id", "")}, {"content", output}});
                        else
                            s->msg().push_back({{"role", "user"}, {"content", nlohmann::json::array({{{"type", "tool_result"}, {"tool_use_id", tc.value("id", "")}, {"content", output}}})}});
                    }
                    continue;
                }
                done = true;
            }
            s->unload();
            cfg.session_id = s->id();
            cell::config::save(cfg);
            if (!interactive)
                running = false;
        }
    }
    catch (const cell::sys::exception &e)
    {
        log.error(std::format("fatal: {}", e.what()));
        cell::sys::error("fatal: {}", e.what());
        s->unload();
        curl_global_cleanup();
        return 1;
    }
    catch (const std::exception &e)
    {
        log.error(std::format("unhandled exception: {}", e.what()));
        cell::sys::error("unhandled exception: {}", e.what());
        s->unload();
        curl_global_cleanup();
        return 1;
    }

    s->unload();
    cfg.session_id = s->id();
    cell::config::save(cfg);
    log.info("cell stopped");
    curl_global_cleanup();
    return 0;
}