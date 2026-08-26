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
            inline bool verbose_enabled = false;
            using clock = std::chrono::steady_clock;

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
        // elapsed milliseconds since a steady-clock time point
        inline long long elapsed_ms(detail::clock::time_point t0)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(detail::clock::now() - t0).count();
        }
        // milliseconds between two steady-clock time points
        inline long long diff_ms(detail::clock::time_point from, detail::clock::time_point to)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
        }
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

            void write(std::string_view level, std::string_view msg, color c, bool console = true)
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
                if (console)
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
            // DEBUG always goes to the log file; console output only with --verbose
            void debug(std::string_view msg) { write("DEBUG", msg, color::yellow, detail::verbose_enabled); }
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
        struct model_entry
        {
            std::string provider = "openai"; // openai | anthropic
            std::string base;                // api base url
            std::string model;               // model name
            std::string key_id;              // vault map key for the api key (optional)

            std::string label() const { return provider + ":" + model; }
            nlohmann::json to_json() const
            {
                nlohmann::json j;
                j["provider"] = provider;
                if (!base.empty())
                    j["base"] = base;
                j["model"] = model;
                if (!key_id.empty())
                    j["key"] = key_id;
                return j;
            }
            static model_entry from_json(const nlohmann::json &j)
            {
                model_entry e;
                e.provider = j.value("provider", "openai");
                e.base = j.value("base", "");
                e.model = j.value("model", "");
                e.key_id = j.value("key", "");
                return e;
            }
        };
        struct settings
        {
            std::vector<model_entry> models;
            size_t current = 0; // index of the active model (first entry is the default)
            std::string system_prompt = "You are a coding agent. Use the provided tools to read, search, write files and run commands when they help. When you finish a task, reply with a short summary of what was done.";
            std::string session_id;

            bool empty() const { return models.empty(); }
            const model_entry *current_entry() const
            {
                if (models.empty())
                    return nullptr;
                return &models[(current < models.size()) ? current : 0];
            }
            model_entry *current_entry()
            {
                if (models.empty())
                    return nullptr;
                return &models[(current < models.size()) ? current : 0];
            }
        };
        std::filesystem::path file() { return root / "config.json"; }

        static model_entry default_model(const std::string &provider)
        {
            model_entry e;
            if (provider == "anthropic")
            {
                e.provider = "anthropic";
                e.base = "https://api.anthropic.com";
                e.model = "claude-3-5-haiku-latest";
            }
            else
            {
                e.provider = "openai";
                e.base = "https://api.openai.com/v1";
                e.model = "gpt-4o-mini";
            }
            return e;
        }
        static settings defaults()
        {
            settings s;
            s.models.push_back(default_model("openai"));
            s.models.push_back(default_model("anthropic"));
            return s;
        }
        // find a model by model-name or "provider:model" exact label
        static int find(const settings &s, const std::string &name, const std::string &provider = "")
        {
            std::string label = provider.empty() ? "" : (provider + ":" + name);
            for (size_t i = 0; i < s.models.size(); i++)
            {
                if (!provider.empty() && s.models[i].label() == label)
                    return (int)i;
                if (provider.empty() && s.models[i].model == name)
                    return (int)i;
            }
            return -1;
        }

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
                if (j.contains("models") && j["models"].is_array())
                {
                    for (auto &mj : j["models"])
                        s.models.push_back(model_entry::from_json(mj));
                    s.current = j.value("current_model", (size_t)0);
                }
                else
                {
                    // legacy flat format -> synthesize a single model entry
                    model_entry e;
                    e.provider = j.value("provider", "openai");
                    e.base = j.value("base", "");
                    e.model = j.value("model", "");
                    if (!e.model.empty())
                        s.models.push_back(std::move(e));
                }
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
            nlohmann::json arr = nlohmann::json::array();
            for (auto &e : s.models)
                arr.push_back(e.to_json());
            j["models"] = arr;
            j["current_model"] = s.current;
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
            bool has(const std::string &map_key) const
            {
                return crypt_map.find(map_key) != crypt_map.end();
            }
            // add or overwrite an existing entry
            bool set(const std::string &map_key, const std::string &raw_value)
            {
                std::string enc = encrypt(raw_value.data(), raw_value.size());
                if (enc.empty())
                    return false;
                crypt_map[map_key] = enc;
                return save();
            }
            bool set(const std::string &map_key, const secure_string &raw_value)
            {
                return set(map_key, raw_value.str());
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
                {
                    cell::sys::logger::instance().warn(std::format("tool {} blocked by deny policy", name()));
                    return false;
                }
                if (policy() == Policy::Ask)
                {
                    std::cout << "allow " << name() << "(" << input << ")? [y/N] " << std::flush;
                    std::string answer;
                    std::getline(std::cin, answer);
                    if (answer != "y" && answer != "Y")
                    {
                        cell::sys::logger::instance().warn(std::format("tool {} rejected by user", name()));
                        return false;
                    }
                    cell::sys::logger::instance().debug(std::format("tool {} approved by user", name()));
                }
                if (!box::check(input))
                {
                    cell::sys::logger::instance().warn(std::format("tool {} blocked by sandbox: {}", name(), input));
                    return false;
                }
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

            bool chat(const encrypt::secure_string &api_key, const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, nlohmann::json &reply, nlohmann::json &tool_calls, nlohmann::json &usage)
            {
                tool_calls = nlohmann::json::array();
                usage = nlohmann::json::object();
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
                    if (j.contains("usage") && j["usage"].is_object())
                        usage = j["usage"];
                    return true;
                }
                catch (const std::exception &e)
                {
                    std::cerr << "openai parse error: " << e.what() << std::endl;
                    return false;
                }
            }

            bool chat_stream(const encrypt::secure_string &api_key, const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, net::StreamCallback on_token, nlohmann::json &reply, nlohmann::json &tool_calls, nlohmann::json &usage)
            {
                tool_calls = nlohmann::json::array();
                usage = nlohmann::json::object();
                std::string text;
                std::string sse_buf;
                std::string url = api_base + "/chat/completions";
                net::StreamCallback cb = [&](std::span<const char> data)
                {
                    sse_buf.append(data.data(), data.size());
                    auto handle = [&](const nlohmann::json &j)
                    {
                        if (j.contains("usage") && j["usage"].is_object())
                            usage = j["usage"];
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

            bool chat(const encrypt::secure_string &api_key, const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, nlohmann::json &reply, nlohmann::json &tool_calls, nlohmann::json &usage)
            {
                tool_calls = nlohmann::json::array();
                usage = nlohmann::json::object();
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
                    if (j.contains("usage") && j["usage"].is_object())
                        usage = j["usage"];
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

            bool chat_stream(const encrypt::secure_string &api_key, const std::string &model, const nlohmann::json &messages, const nlohmann::json &tools, net::StreamCallback on_token, nlohmann::json &reply, nlohmann::json &tool_calls, nlohmann::json &usage)
            {
                tool_calls = nlohmann::json::array();
                usage = nlohmann::json::object();
                std::vector<nlohmann::json> blocks;
                std::string sse_buf;
                std::string url = api_base + "/v1/messages";
                net::StreamCallback cb = [&](std::span<const char> data)
                {
                    sse_buf.append(data.data(), data.size());
                    auto handle = [&](const nlohmann::json &ev)
                    {
                        std::string type = ev.value("type", "");
                        if (type == "message_start" && ev.contains("message") && ev["message"].contains("usage"))
                            usage["input_tokens"] = ev["message"]["usage"].value("input_tokens", 0);
                        else if (type == "message_delta" && ev.contains("usage"))
                            usage["output_tokens"] = ev["usage"].value("output_tokens", 0);
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
                {
                    cell::sys::logger::instance().debug(std::format("session cache hit: {} (in memory)", session_id));
                    return;
                }
                loaded = true;
                if (!std::filesystem::exists(file))
                {
                    cell::sys::logger::instance().debug(std::format("session created: {} (no file on disk)", session_id));
                    return;
                }
                std::ifstream f(file);
                try
                {
                    auto j = nlohmann::json::parse(f, nullptr, false);
                    if (j.contains("messages") && j["messages"].is_array())
                    {
                        messages = j["messages"];
                        cell::sys::logger::instance().info(std::format("session loaded from disk: {} ({} messages)", session_id, messages.size()));
                    }
                }
                catch (const std::exception &)
                {
                    messages = nlohmann::json::array();
                    cell::sys::logger::instance().warn(std::format("session file corrupt, started empty: {}", session_id));
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
                    cell::sys::logger::instance().debug(std::format("session map miss: creating {}", current));
                    session s = (current == "current") ? session() : session(current);
                    it = session_list.emplace(current, std::move(s)).first;
                }
                else
                {
                    cell::sys::logger::instance().debug(std::format("session map hit: {}", current));
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
    namespace skills
    {
        struct skill
        {
            std::string name;
            std::string description;
            std::string file; // filename under .cell/skills/
        };
        static std::string trim(std::string_view s)
        {
            size_t b = 0, e = s.size();
            while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r'))
                b++;
            while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
                e--;
            return std::string(s.substr(b, e - b));
        }
        static std::string strip_bom(std::string s)
        {
            if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
                s.erase(0, 3);
            return s;
        }
        static std::vector<std::string> lines(const std::string &text)
        {
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
        }
        // parse optional YAML-style front matter: "---\nname: x\ndescription: y\n---\n body"
        static void parse_metadata(const std::string &raw, skill &s)
        {
            std::string text = strip_bom(raw);
            std::string body = text;
            if (text.rfind("---", 0) == 0)
            {
                size_t end = text.find("\n---");
                if (end != std::string::npos)
                {
                    std::string meta = text.substr(3, end - 3);
                    for (auto &line : lines(meta))
                    {
                        size_t colon = line.find(':');
                        if (colon == std::string::npos)
                            continue;
                        std::string key = trim(std::string_view(line).substr(0, colon));
                        std::string val = trim(std::string_view(line).substr(colon + 1));
                        if (key == "name")
                            s.name = val;
                        else if (key == "description")
                            s.description = val;
                    }
                    body = text.substr(end + 4);
                }
            }
            if (s.name.empty())
                s.name = std::filesystem::path(s.file).stem().string();
            if (s.description.empty())
            {
                for (auto &line : lines(body))
                {
                    if (!trim(line).empty())
                    {
                        s.description = trim(line);
                        if (s.description.size() > 120)
                            s.description = s.description.substr(0, 117) + "...";
                        break;
                    }
                }
            }
        }
        static std::vector<skill> list()
        {
            std::vector<skill> out;
            std::filesystem::path dir = root / "skills";
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec))
                return out;
            for (auto &entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec))
                    continue;
                if (entry.path().extension() != ".md")
                    continue;
                std::string text;
                if (!box::read(entry.path().string(), text) || text.empty())
                    continue;
                skill s;
                s.file = entry.path().filename().string();
                parse_metadata(text, s);
                out.push_back(std::move(s));
            }
            std::sort(out.begin(), out.end(), [](const skill &a, const skill &b) { return a.name < b.name; });
            return out;
        }
        static const skill *find(const std::vector<skill> &all, const std::string &name)
        {
            for (auto &s : all)
            {
                if (s.name == name)
                    return &s;
            }
            return nullptr;
        }
        // read the full body of a skill (front matter stripped)
        static bool content(const skill &s, std::string &out)
        {
            std::string text;
            if (!box::read((root / "skills" / s.file).string(), text))
                return false;
            text = strip_bom(std::move(text));
            if (text.rfind("---", 0) == 0)
            {
                size_t end = text.find("\n---");
                if (end != std::string::npos)
                    text = text.substr(end + 4);
            }
            out = std::move(text);
            return true;
        }
        // metadata-only prompt injection listing available skills
        static std::string metadata_prompt(const std::vector<skill> &all)
        {
            if (all.empty())
                return "";
            std::string out = "The following skills are available in this workspace. Each can be loaded with the /skill command by the user. When a task matches a skill, suggest the user load it.\nAvailable skills:\n";
            for (auto &s : all)
                out += std::format("- {}{}\n", s.name, s.description.empty() ? "" : ": " + s.description);
            return out;
        }
    } // namespace skills
    namespace stats
    {
        static std::filesystem::path file() { return root / "usages.json"; }
        static nlohmann::json load()
        {
            std::ifstream f(file());
            if (!f.is_open())
                return nlohmann::json{{"sessions", nlohmann::json::object()}, {"models", nlohmann::json::object()}};
            try
            {
                auto j = nlohmann::json::parse(f, nullptr, false);
                if (j.is_object() && j.contains("sessions") && j.contains("models"))
                    return j;
            }
            catch (const std::exception &)
            {
            }
            return nlohmann::json{{"sessions", nlohmann::json::object()}, {"models", nlohmann::json::object()}};
        }
        static bool save(const nlohmann::json &j)
        {
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
            std::ofstream f(file(), std::ios::trunc);
            if (!f.is_open())
                return false;
            f << j.dump(2);
            return f.good();
        }
        // record one llm request against a session + model
        static void add(const std::string &session_id, const std::string &model,
                        long long input_chars, long long output_chars,
                        std::optional<long long> input_tokens, std::optional<long long> output_tokens,
                        long long messages = 0)
        {
            nlohmann::json j = load();
            auto bump = [&](nlohmann::json &rec)
            {
                rec["requests"] = rec.value("requests", 0LL) + 1;
                rec["messages"] = rec.value("messages", 0LL) + messages;
                rec["input_chars"] = rec.value("input_chars", 0LL) + input_chars;
                rec["output_chars"] = rec.value("output_chars", 0LL) + output_chars;
                if (input_tokens)
                    rec["input_tokens"] = rec.value("input_tokens", 0LL) + *input_tokens;
                if (output_tokens)
                    rec["output_tokens"] = rec.value("output_tokens", 0LL) + *output_tokens;
            };
            auto &sess = j["sessions"][session_id];
            if (!sess.is_object())
                sess = nlohmann::json::object();
            sess["model"] = model;
            bump(sess);
            auto &m = j["models"][model];
            if (!m.is_object())
                m = nlohmann::json::object();
            bump(m);
            save(j);
        }
        static std::string fmt(const nlohmann::json &rec)
        {
            return std::format("requests={} messages={} in_chars={} out_chars={} in_tok={} out_tok={}",
                               rec.value("requests", 0LL), rec.value("messages", 0LL),
                               rec.value("input_chars", 0LL), rec.value("output_chars", 0LL),
                               rec.value("input_tokens", 0LL), rec.value("output_tokens", 0LL));
        }
        static std::string summarize()
        {
            nlohmann::json j = load();
            std::string out = "usage statistics (.cell/usages.json)\n\nper model:\n";
            if (j["models"].empty())
                out += "  (none)\n";
            for (auto &[k, v] : j["models"].items())
                out += std::format("  {}  {}\n", k, fmt(v));
            out += "\nper session:\n";
            if (j["sessions"].empty())
                out += "  (none)\n";
            for (auto &[k, v] : j["sessions"].items())
                out += std::format("  {}  model={}  {}\n", k, v.value("model", "?"), fmt(v));
            return out;
        }
    } // namespace stats
} // namespace cell

static void print_usage(const char *prog)
{
    cell::sys::println("usage: {} [options]", prog);
    cell::sys::println("  --provider openai|anthropic  default llm provider");
    cell::sys::println("  --base URL                   api base url for the default model");
    cell::sys::println("  --model MODEL                default model name");
    cell::sys::println("  --key KEY                    api key (saved to the encrypted vault)");
    cell::sys::println("  --session ID                 resume an existing session");
    cell::sys::println("  --system TEXT                system prompt");
    cell::sys::println("  --no-color                   disable colored log output");
    cell::sys::println("  --verbose                    enable DEBUG-level log output on console");
    cell::sys::println("  --selftest                   run internal self tests");
}

static void print_help()
{
    cell::sys::println("commands:");
    cell::sys::println("  /models                     list configured models");
    cell::sys::println("  /model NAME                 switch to a configured model (NAME or provider:NAME)");
    cell::sys::println("  /model provider:NAME base:URL key:KEY   add/update a model");
    cell::sys::println("      e.g. /model anthropic:claude-opus4.8 base:https://api.anthropic.com key:sk-xxx");
    cell::sys::println("  /sessions                   list saved sessions");
    cell::sys::println("  /usages                     show per-model and per-session usage statistics");
    cell::sys::println("  /compact                    compress the current session context");
    cell::sys::println("  /skills                     list available skills (.cell/skills/*.md)");
    cell::sys::println("  /skill NAME                 load a skill into the session");
    cell::sys::println("  /save                       save the current session");
    cell::sys::println("  /new                        start a fresh session");
    cell::sys::println("  /exit | /quit               exit");
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
    log.debug("selftest debug");
    expect(cell::box::exist((cell::root / "logs" / "cell.log").string()), "logger writes log file");
    expect(cell::box::read((cell::root / "logs" / "cell.log").string(), out) && out.find("selftest debug") != std::string::npos, "logger debug writes to log file");

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
    cell::config::model_entry a;
    a.provider = "openai";
    a.base = "http://x/v1";
    a.model = "m1";
    a.key_id = "k1";
    cell::config::model_entry b;
    b.provider = "anthropic";
    b.base = "http://y";
    b.model = "m2";
    cfg.models = {a, b};
    cfg.current = 1;
    cfg.system_prompt = "sys";
    expect(cell::config::save(cfg), "config::save multi-model");
    auto cfg_res = cell::config::load();
    expect(cfg_res.has_value() && cfg_res->models.size() == 2, "config::load multi-model");
    expect(cfg_res.has_value() && cfg_res->current == 1 && cfg_res->current_entry()->model == "m2", "config current entry");
    expect(cfg_res.has_value() && cfg_res->models[0].label() == "openai:m1", "config model label");
    expect(cell::config::find(*cfg_res, "m1") == 0 && cell::config::find(*cfg_res, "m2", "anthropic") == 1 && cell::config::find(*cfg_res, "nope") == -1, "config::find");
    cell::box::write((cell::root / "config.json").string(), "{\"provider\":\"anthropic\",\"model\":\"legacy\",\"base\":\"http://z\",\"session\":\"s1\"}");
    auto legacy_res = cell::config::load();
    expect(legacy_res.has_value() && legacy_res->models.size() == 1 && legacy_res->models[0].provider == "anthropic" && legacy_res->models[0].model == "legacy", "config legacy flat load");
    expect(legacy_res.has_value() && legacy_res->session_id == "s1", "config legacy session");
    cell::box::write((cell::root / "config.json").string(), "{invalid");
    expect(!cell::config::load().has_value(), "config::load reports parse error");

    expect(vault.set("overwrite_key", "v1") && vault.get("overwrite_key") == "v1", "crypt::set new");
    expect(vault.set("overwrite_key", "v2") && vault.get("overwrite_key") == "v2", "crypt::set overwrite");
    expect(vault.has("overwrite_key") && !vault.has("missing_key"), "crypt::has");

    expect(cell::box::mkdir((cell::root / "skills").string()), "skills dir");
    std::string skill_md = "---\nname: build-helper\ndescription: helpers for building cell\n---\n# Build Helper\nfull body instructions\n";
    expect(cell::box::write((cell::root / "skills" / "build-helper.md").string(), skill_md), "skill file");
    expect(cell::box::write((cell::root / "skills" / "plain.md").string(), "First line is the description.\nrest of body\n"), "skill plain file");
    auto skills = cell::skills::list();
    expect(skills.size() == 2, "skills::list");
    const cell::skills::skill *found = nullptr;
    for (auto &sk : skills)
        if (sk.name == "build-helper")
            found = &sk;
    expect(found && found->description.find("helpers for building cell") != std::string::npos, "skill metadata parse");
    bool plain_ok = false;
    for (auto &sk : skills)
        if (sk.name == "plain")
            plain_ok = sk.description.find("First line") != std::string::npos;
    expect(plain_ok, "skill fallback name+first line");
    std::string skill_body;
    expect(cell::skills::content(*found, skill_body) && skill_body.find("# Build Helper") != std::string::npos && skill_body.find("---") == std::string::npos, "skill content strips front matter");
    std::string meta = cell::skills::metadata_prompt(skills);
    expect(meta.find("build-helper") != std::string::npos && meta.find("plain") != std::string::npos, "skill metadata prompt");

    cell::stats::add("sess-A", "openai:gpt-4o", 100, 50, 10, 5, 2);
    cell::stats::add("sess-A", "openai:gpt-4o", 50, 20, std::nullopt, std::nullopt, 1);
    cell::stats::add("sess-B", "anthropic:claude-x", 30, 10, 3, 1, 1);
    auto stats_json = cell::stats::load();
    expect(stats_json["models"]["openai:gpt-4o"].value("requests", 0LL) == 2, "stats per-model requests");
    expect(stats_json["models"]["openai:gpt-4o"].value("input_chars", 0LL) == 150, "stats per-model input_chars");
    expect(stats_json["sessions"]["sess-A"].value("messages", 0LL) == 3, "stats per-session messages");
    expect(stats_json["models"]["anthropic:claude-x"].value("input_tokens", 0LL) == 3, "stats tokens");
    expect(cell::stats::summarize().find("openai:gpt-4o") != std::string::npos, "stats summarize");

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
    bool no_color = false;
    bool verbose = false;
    std::string key_arg;
    std::string provider_arg, base_arg, model_arg;
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
            provider_arg = cell::skills::trim(value("--provider"));
        else if (arg == "--base")
            base_arg = cell::skills::trim(value("--base"));
        else if (arg == "--model")
            model_arg = cell::skills::trim(value("--model"));
        else if (arg == "--key")
            key_arg = cell::skills::trim(value("--key"));
        else if (arg == "--session")
            cfg.session_id = value("--session");
        else if (arg == "--system")
            cfg.system_prompt = value("--system");
        else if (arg == "--no-color")
            no_color = true;
        else if (arg == "--verbose")
            verbose = true;
        else if (arg == "--selftest")
            selftest = true;
        else
        {
            cell::sys::error("unknown argument: {}", arg);
            print_usage(argv[0]);
            return 1;
        }
    }
    cell::sys::detail::color_force = !no_color;
    cell::sys::detail::verbose_enabled = verbose;
    cell::sys::detail::init_console();
    cell::sys::install_handlers();
    if (selftest)
        return run_selftest();

    curl_global_init(CURL_GLOBAL_DEFAULT);
    int sodium_rc = sodium_init(); (void)sodium_rc;
    cell::encrypt::crypt vault;
    auto &log = cell::sys::logger::instance();

    // make sure at least the default models exist (first entry is the default)
    if (cfg.empty())
        cfg = cell::config::defaults();
    // apply CLI overrides onto the current model entry
    if (!provider_arg.empty() || !model_arg.empty() || !base_arg.empty())
    {
        if (cell::config::model_entry *e = cfg.current_entry(); e)
        {
            if (!provider_arg.empty())
                e->provider = provider_arg;
            if (!model_arg.empty())
                e->model = model_arg;
            if (!base_arg.empty())
                e->base = base_arg;
        }
        else
        {
            cell::config::model_entry ne;
            ne.provider = provider_arg.empty() ? "openai" : provider_arg;
            ne.model = model_arg;
            ne.base = base_arg;
            cfg.models.push_back(std::move(ne));
            cfg.current = cfg.models.size() - 1;
        }
    }
    if (!key_arg.empty())
    {
        std::string kid = "model:" + (cfg.current_entry() ? cfg.current_entry()->label() : "default");
        if (cell::config::model_entry *e = cfg.current_entry(); e)
            e->key_id = kid;
        vault.set(kid, key_arg);
        sodium_memzero(key_arg.data(), key_arg.size());
    }
    if (const cell::config::model_entry *e = cfg.current_entry(); e)
        log.info(std::format("cell started, {} model(s) configured, current: {} (provider={}, base={})",
                             cfg.models.size(), e->label(), e->provider, e->base.empty() ? "(default)" : e->base));
    else
        log.info(std::format("cell started, {} model(s) configured, current: none", cfg.models.size()));

    // tool definitions for both API styles
    auto tools_o = build_tools(false);
    auto tools_a = build_tools(true);
    auto &tool_list = tools_o.first;
    const nlohmann::json &tool_defs_openai = tools_o.second;
    const nlohmann::json &tool_defs_anthropic = tools_a.second;

    // per-(provider, base) client cache
    struct client_cache
    {
        std::unordered_map<std::string, std::unique_ptr<cell::llm::OpenAI>> openai;
        std::unordered_map<std::string, std::unique_ptr<cell::llm::Anthropic>> anthropic;
        cell::llm::OpenAI &o(const std::string &base)
        {
            auto it = openai.find(base);
            if (it != openai.end())
            {
                cell::sys::logger::instance().debug(std::format("client cache hit: openai [{}]", base));
                return *it->second;
            }
            cell::sys::logger::instance().debug(std::format("client cache miss: creating openai client [{}]", base));
            auto &p = openai[base];
            p = std::make_unique<cell::llm::OpenAI>(base);
            return *p;
        }
        cell::llm::Anthropic &a(const std::string &base)
        {
            auto it = anthropic.find(base);
            if (it != anthropic.end())
            {
                cell::sys::logger::instance().debug(std::format("client cache hit: anthropic [{}]", base));
                return *it->second;
            }
            cell::sys::logger::instance().debug(std::format("client cache miss: creating anthropic client [{}]", base));
            auto &p = anthropic[base];
            p = std::make_unique<cell::llm::Anthropic>(base);
            return *p;
        }
    } cache;

    auto resolve_key = [&](const cell::config::model_entry &e) -> cell::encrypt::secure_string
    {
        if (!e.key_id.empty())
        {
            auto k = vault.get(e.key_id);
            if (!k.empty())
                return k;
        }
        const char *env = std::getenv(e.provider == "anthropic" ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY");
        if (env && *env)
            return cell::encrypt::secure_string(env);
        return vault.get("api_key");
    };

    // unified request dispatch: picks the right client + tool schema for any model entry
    auto do_chat = [&](const cell::config::model_entry &e, const cell::encrypt::secure_string &key,
                       const nlohmann::json &msgs, bool stream, cell::net::StreamCallback on_tok,
                       nlohmann::json &reply, nlohmann::json &tc, nlohmann::json &usage) -> bool
    {
        bool ap = e.provider == "anthropic";
        const nlohmann::json &tools = ap ? tool_defs_anthropic : tool_defs_openai;
        if (ap)
        {
            if (stream)
                return cache.a(e.base).chat_stream(key, e.model, msgs, tools, on_tok, reply, tc, usage);
            return cache.a(e.base).chat(key, e.model, msgs, tools, reply, tc, usage);
        }
        if (stream)
            return cache.o(e.base).chat_stream(key, e.model, msgs, tools, on_tok, reply, tc, usage);
        return cache.o(e.base).chat(key, e.model, msgs, tools, reply, tc, usage);
    };

    auto on_token = [](std::span<const char> data)
    {
        std::fwrite(data.data(), 1, data.size(), stdout);
        std::fflush(stdout);
    };
    auto trunc = [](const std::string &s, size_t n) -> std::string
    {
        return s.size() <= n ? s : s.substr(0, n) + "...";
    };

    auto content_chars = [](const nlohmann::json &msgs) -> long long
    {
        long long n = 0;
        for (auto &m : msgs)
        {
            auto it = m.find("content");
            if (it == m.end())
                continue;
            if (it->is_string())
                n += (long long)it->get_ref<const std::string &>().size();
            else if (it->is_array())
                n += (long long)it->dump().size();
        }
        return n;
    };
    auto reply_text = [](const nlohmann::json &reply) -> std::string
    {
        auto it = reply.find("content");
        if (it == reply.end())
            return "";
        if (it->is_string())
            return it->get<std::string>();
        if (it->is_array())
        {
            std::string t;
            for (auto &b : *it)
                if (b.value("type", "") == "text" && b.contains("text"))
                    t += b["text"].get<std::string>();
            return t;
        }
        return "";
    };
    auto reply_text_len = [&](const nlohmann::json &reply) -> long long
    {
        return (long long)reply_text(reply).size();
    };
    auto usage_in = [](const nlohmann::json &u) -> std::optional<long long>
    {
        if (u.is_object())
        {
            if (u.contains("prompt_tokens") && u["prompt_tokens"].is_number_integer())
                return u["prompt_tokens"].get<long long>();
            if (u.contains("input_tokens") && u["input_tokens"].is_number_integer())
                return u["input_tokens"].get<long long>();
        }
        return std::nullopt;
    };
    auto usage_out = [](const nlohmann::json &u) -> std::optional<long long>
    {
        if (u.is_object())
        {
            if (u.contains("completion_tokens") && u["completion_tokens"].is_number_integer())
                return u["completion_tokens"].get<long long>();
            if (u.contains("output_tokens") && u["output_tokens"].is_number_integer())
                return u["output_tokens"].get<long long>();
        }
        return std::nullopt;
    };

    // session + skills prompt injection
    cell::chat::history h;
    if (!cfg.session_id.empty())
        h.use(cfg.session_id);
    cell::chat::session *s = &h.now();
    auto skills_all = cell::skills::list();
    std::string skills_prompt = cell::skills::metadata_prompt(skills_all);
    auto ensure_prompt = [&](cell::chat::session *sess)
    {
        if (!sess->msg().empty())
            return;
        sess->msg().push_back({{"role", "system"}, {"content", cfg.system_prompt}});
        if (!skills_prompt.empty())
            sess->msg().push_back({{"role", "system"}, {"content", skills_prompt}});
        log.debug(std::format("prompt injection: system prompt ({} chars){}", cfg.system_prompt.size(),
                              skills_prompt.empty() ? "" : std::format(", skills metadata ({} chars)", skills_prompt.size())));
    };
    ensure_prompt(s);
    cell::sys::println("cell: session={} model={}", s->id(), cfg.current_entry() ? cfg.current_entry()->label() : "none");

    auto list_models = [&]()
    {
        if (cfg.models.empty())
        {
            cell::sys::println("  (no models configured)");
            return;
        }
        for (size_t i = 0; i < cfg.models.size(); i++)
        {
            auto &e = cfg.models[i];
            cell::sys::println("  [{}] {}{}", i, e.label(), (i == cfg.current) ? "  <current>" : "");
            if (!e.base.empty())
                cell::sys::println("       base: {}", e.base);
            if (!e.key_id.empty())
                cell::sys::println("       key:  stored");
        }
    };

    // context compaction: keep system messages + head/tail, summarize the middle via the llm
    auto compact = [&](cell::chat::session *sess) -> std::string
    {
        auto &msgs = sess->msg();
        nlohmann::json sys = nlohmann::json::array();
        nlohmann::json rest = nlohmann::json::array();
        for (auto &m : msgs)
        {
            if (m.value("role", "") == "system")
                sys.push_back(m);
            else
                rest.push_back(m);
        }
        if (rest.size() <= 12)
            return std::format("context already small ({} non-system messages)", rest.size());
        const size_t head_n = 2, tail_n = 6;
        nlohmann::json keep_head(rest.begin(), rest.begin() + (ptrdiff_t)head_n);
        nlohmann::json keep_tail(rest.end() - (ptrdiff_t)tail_n, rest.end());
        nlohmann::json middle(rest.begin() + (ptrdiff_t)head_n, rest.end() - (ptrdiff_t)tail_n);
        std::string mid_text;
        for (auto &m : middle)
        {
            std::string role = m.value("role", "");
            if (role == "tool")
                continue;
            std::string body;
            auto it = m.find("content");
            if (it != m.end())
            {
                if (it->is_string())
                    body = it->get<std::string>();
                else if (it->is_array())
                    for (auto &b : *it)
                        body += b.dump() + "\n";
            }
            if (body.size() > 400)
                body = body.substr(0, 400) + "...";
            mid_text += std::format("{}: {}\n", role, body);
        }
        std::string summary = "(llm summarization unavailable; messages truncated)";
        if (const cell::config::model_entry *e = cfg.current_entry(); e)
        {
            cell::encrypt::secure_string key = resolve_key(*e);
            if (!key.empty())
            {
                nlohmann::json prompt = nlohmann::json::array();
                prompt.push_back({{"role", "user"},
                                  {"content", std::format("Summarize the following removed part of a coding-agent conversation concisely, preserving key decisions, facts, file paths and unfinished tasks. Output only the summary.\n\n{}", mid_text)}});
                nlohmann::json reply, tc, usage;
                log.info(std::format("compact: summarizing {} removed message(s) via {}", middle.size(), e->label()));
                cell::sys::print("summary> ");
                auto t0 = cell::sys::detail::clock::now();
                if (do_chat(*e, key, prompt, true, on_token, reply, tc, usage))
                {
                    long long ms = cell::sys::elapsed_ms(t0);
                    cell::sys::println();
                    summary = reply_text(reply);
                    if (summary.empty())
                        summary = "(empty summary)";
                    auto in_tok = usage_in(usage);
                    auto out_tok = usage_out(usage);
                    log.info(std::format("compact summary: {} chars, {}ms, in_tok={}, out_tok={}",
                                         summary.size(), ms,
                                         in_tok.has_value() ? std::to_string(*in_tok) : "n/a",
                                         out_tok.has_value() ? std::to_string(*out_tok) : "n/a"));
                    cell::stats::add(sess->id(), e->label(), content_chars(prompt), (long long)summary.size(), usage_in(usage), usage_out(usage), 0);
                }
                else
                {
                    cell::sys::println();
                    log.warn("compact: llm summarization failed, falling back to truncation");
                }
            }
        }
        // rebuild kept messages as plain text to avoid orphaned tool_call_ids
        auto plain = [](const nlohmann::json &m) -> nlohmann::json
        {
            nlohmann::json out;
            out["role"] = m.value("role", "");
            auto it = m.find("content");
            if (it != m.end() && it->is_string())
                out["content"] = *it;
            else if (it != m.end() && it->is_array())
            {
                std::string t;
                for (auto &b : *it)
                    if (b.value("type", "") == "text" && b.contains("text"))
                        t += b["text"].get<std::string>();
                out["content"] = t;
            }
            else
                out["content"] = "";
            return out;
        };
        nlohmann::json new_msgs = nlohmann::json::array();
        for (auto &m : sys)
            new_msgs.push_back(m);
        new_msgs.push_back({{"role", "system"},
                            {"content", std::format("The middle part of this conversation was removed for length. Summary of the removed part:\n{}", summary)}});
        for (auto &m : keep_head)
            new_msgs.push_back(plain(m));
        for (auto &m : keep_tail)
            new_msgs.push_back(plain(m));
        size_t removed = middle.size();
        msgs = new_msgs;
        return std::format("context compacted: removed {} message(s), {} remain", removed, new_msgs.size());
    };

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
            if (input.size() >= 3 && (unsigned char)input[0] == 0xEF && (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
                input.erase(0, 3);
            if (input.empty())
                continue;

            if (input[0] == '/')
            {
                std::vector<std::string> toks;
                {
                    std::istringstream ss(input);
                    std::string t;
                    while (ss >> t)
                        toks.push_back(t);
                }
                std::string cmd = toks.empty() ? "" : toks[0];
                log.info(std::format("command: {}", cmd));
                if (cmd == "/exit" || cmd == "/quit")
                    break;
                if (cmd == "/help")
                {
                    print_help();
                    continue;
                }
                if (cmd == "/save")
                {
                    s->unload();
                    log.info(std::format("session saved: {}", s->id()));
                    cell::sys::println("session saved: {}", s->id());
                    continue;
                }
                if (cmd == "/new")
                {
                    std::string old_id = s->id();
                    h.remove(old_id);
                    s = &h.now();
                    ensure_prompt(s);
                    log.info(std::format("new session: {} (previous {} discarded)", s->id(), old_id));
                    cell::sys::println("new session: {}", s->id());
                    continue;
                }
                if (cmd == "/models")
                {
                    list_models();
                    continue;
                }
                if (cmd == "/model")
                {
                    if (toks.size() < 2)
                    {
                        list_models();
                        continue;
                    }
                    std::string target = toks[1];
                    std::string p, m;
                    bool spaced_form = false;
                    size_t colon = target.find(':');
                    if (colon != std::string::npos && colon != 0 && colon + 1 < target.size())
                    {
                        // "provider:model" in one token, e.g. anthropic:claude-opus4.8
                        p = target.substr(0, colon);
                        m = target.substr(colon + 1);
                    }
                    else if (colon != std::string::npos && colon != 0 && colon + 1 == target.size())
                    {
                        // tolerant spaced form "provider: model" (any number of separating spaces)
                        if (toks.size() < 3)
                        {
                            cell::sys::error("missing model name after '{}'", target);
                            continue;
                        }
                        p = target.substr(0, colon);
                        m = toks[2];
                        toks.erase(toks.begin() + 2);
                        spaced_form = true;
                    }
                    else
                    {
                        m = target;
                        p = cfg.current_entry() ? cfg.current_entry()->provider : "openai";
                    }
                    std::string new_base, new_key;
                    bool has_opts = false;
                    for (size_t i = 2; i < toks.size(); i++)
                    {
                        auto &t = toks[i];
                        if (t.rfind("base:", 0) == 0)
                        {
                            // tolerant spaced form: "base: https://..." (value in the next token)
                            if (t.size() > 5)
                                new_base = t.substr(5);
                            else if (i + 1 < toks.size())
                                new_base = toks[++i];
                            has_opts = true;
                        }
                        else if (t.rfind("key:", 0) == 0)
                        {
                            // tolerant spaced form: "key: sk-..." (value in the next token)
                            if (t.size() > 4)
                                new_key = t.substr(4);
                            else if (i + 1 < toks.size())
                                new_key = toks[++i];
                            has_opts = true;
                        }
                        else
                            cell::sys::warn("ignoring token: {}", t);
                    }
                    new_base = cell::skills::trim(new_base);
                    new_key = cell::skills::trim(new_key);
                    if (has_opts)
                    {
                        // add or update a model
                        int idx = cell::config::find(cfg, m, p);
                        if (idx < 0)
                        {
                            cell::config::model_entry e;
                            e.provider = p;
                            e.model = m;
                            e.base = new_base;
                            cfg.models.push_back(std::move(e));
                            idx = (int)cfg.models.size() - 1;
                        }
                        else if (!new_base.empty())
                        {
                            cfg.models[idx].base = new_base;
                        }
                        if (!new_key.empty())
                        {
                            std::string kid = "model:" + cfg.models[idx].label();
                            vault.set(kid, new_key);
                            sodium_memzero(new_key.data(), new_key.size());
                            cfg.models[idx].key_id = kid;
                        }
                        cfg.current = (size_t)idx;
                        cell::config::save(cfg);
                        auto &reg = cfg.models[idx];
                        log.info(std::format("model registered: {}", reg.label()));
                        cell::sys::println("model registered: {}", reg.label());
                        cell::sys::println("       provider: {}", reg.provider);
                        cell::sys::println("       model:    {}", reg.model);
                        cell::sys::println("       base:     {}", reg.base.empty() ? "(default)" : reg.base);
                        cell::sys::println("       key:      {}", reg.key_id.empty() ? "not stored (env var / vault fallback)" : "stored (encrypted vault)");
                        if (spaced_form)
                            cell::sys::println("note: spaced form used; the registered model name is \"{}\", not \" {}\"", reg.model, reg.model);
                        continue;
                    }
                    int idx = cell::config::find(cfg, m, p);
                    if (idx < 0)
                    {
                        cell::sys::error("model not found: {} (use /model to add)", target);
                        continue;
                    }
                    cfg.current = (size_t)idx;
                    cell::config::save(cfg);
                    log.info(std::format("model switched: {}", cfg.models[idx].label()));
                    cell::sys::println("switched to {}", cfg.models[idx].label());
                    continue;
                }
                if (cmd == "/sessions")
                {
                    std::error_code ec;
                    std::filesystem::path dir = cell::root / "sessions";
                    bool any = false;
                    if (std::filesystem::exists(dir, ec))
                    {
                        for (auto &entry : std::filesystem::directory_iterator(dir, ec))
                        {
                            if (ec)
                                break;
                            if (!entry.is_regular_file(ec))
                                continue;
                            if (entry.path().extension() != ".json")
                                continue;
                            any = true;
                            std::string sid = entry.path().stem().string();
                            std::string marker = (sid == s->id()) ? " *" : "";
                            long long count = 0;
                            std::string snippet;
                            try
                            {
                                std::ifstream f(entry.path());
                                auto j = nlohmann::json::parse(f, nullptr, false);
                                if (j.contains("messages") && j["messages"].is_array())
                                {
                                    count = (long long)j["messages"].size();
                                    for (auto &m : j["messages"])
                                    {
                                        if (m.value("role", "") == "user" && m.contains("content") && m["content"].is_string())
                                        {
                                            snippet = m["content"].get<std::string>();
                                            break;
                                        }
                                    }
                                }
                            }
                            catch (const std::exception &)
                            {
                            }
                            if (snippet.size() > 60)
                                snippet = snippet.substr(0, 57) + "...";
                            cell::sys::println("  {}{}  messages={}{}", sid, marker, count, snippet.empty() ? "" : "  \"" + snippet + "\"");
                        }
                    }
                    if (!any)
                        cell::sys::println("  (no sessions)");
                    continue;
                }
                if (cmd == "/usages")
                {
                    cell::sys::println("{}", cell::stats::summarize());
                    continue;
                }
                if (cmd == "/compact")
                {
                    cell::sys::println("{}", compact(s));
                    s->unload();
                    cell::config::save(cfg);
                    continue;
                }
                if (cmd == "/skills")
                {
                    auto all = cell::skills::list();
                    if (all.empty())
                    {
                        cell::sys::println("  (no skills in {})", (cell::root / "skills").string());
                        continue;
                    }
                    for (auto &sk : all)
                        cell::sys::println("  {}{}", sk.name, sk.description.empty() ? "" : ": " + sk.description);
                    continue;
                }
                if (cmd == "/skill")
                {
                    if (toks.size() < 2)
                    {
                        cell::sys::error("usage: /skill NAME");
                        continue;
                    }
                    auto all = cell::skills::list();
                    const cell::skills::skill *sk = cell::skills::find(all, toks[1]);
                    if (!sk)
                    {
                        cell::sys::error("skill not found: {}", toks[1]);
                        continue;
                    }
                    std::string body;
                    if (!cell::skills::content(*sk, body))
                    {
                        cell::sys::error("failed to read skill: {}", toks[1]);
                        continue;
                    }
                    s->msg().push_back({{"role", "system"},
                                        {"content", std::format("You have loaded the skill \"{}\". Follow its instructions for the rest of this session.\n\n{}", sk->name, body)}});
                    s->unload();
                    log.info(std::format("prompt injection: skill {} ({} chars)", sk->name, body.size()));
                    cell::sys::println("skill loaded: {} ({} chars)", sk->name, body.size());
                    continue;
                }
                cell::sys::error("unknown command: {} (try /help)", cmd);
                continue;
            }

            s->msg().push_back({{"role", "user"}, {"content", input}});
            log.info(std::format("user: {}", input));
            cell::sys::print("reply> ");

            bool done = false;
            int rounds = 0;
            while (!done && rounds++ < 8)
            {
                const cell::config::model_entry *e = cfg.current_entry();
                if (!e)
                {
                    cell::sys::error("no model configured");
                    done = true;
                    break;
                }
                cell::encrypt::secure_string key = resolve_key(*e);
                if (key.empty())
                {
                    cell::sys::error("no api key for {}: set the {} env var, or use /model {} base:URL key:KEY",
                                     e->label(), e->provider == "anthropic" ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY", e->label());
                    done = true;
                    break;
                }
                size_t before = s->msg().size();
                long long in_chars = content_chars(s->msg());
                nlohmann::json reply, tool_calls, usage;
                auto t0 = cell::sys::detail::clock::now();
                bool first_token = true;
                auto tok0 = t0;
                cell::net::StreamCallback tok_cb = [&](std::span<const char> data)
                {
                    if (first_token && !data.empty())
                    {
                        first_token = false;
                        tok0 = cell::sys::detail::clock::now();
                    }
                    on_token(data);
                };
                bool ok = do_chat(*e, key, s->msg(), true, tok_cb, reply, tool_calls, usage);
                long long total_ms = cell::sys::elapsed_ms(t0);
                long long ttf_ms = first_token ? -1 : cell::sys::diff_ms(t0, tok0);
                if (!ok)
                {
                    log.error(std::format("llm request failed: {} ({}ms)", e->label(), total_ms));
                    cell::sys::println();
                    cell::sys::error("[llm request failed]");
                    done = true;
                    break;
                }
                cell::sys::println();
                long long out_chars = reply_text_len(reply);
                auto in_tok = usage_in(usage);
                auto out_tok = usage_out(usage);
                log.info(std::format("llm round {}: {} [stream] in_chars={} out_chars={} in_tok={} out_tok={} {}ms (ttf {})",
                                     rounds, e->label(), in_chars, out_chars,
                                     in_tok.has_value() ? std::to_string(*in_tok) : "n/a",
                                     out_tok.has_value() ? std::to_string(*out_tok) : "n/a",
                                     total_ms, ttf_ms < 0 ? "n/a" : std::format("{}ms", ttf_ms)));
                s->msg().push_back(reply);
                if (!tool_calls.empty())
                {
                    for (auto &tc : tool_calls)
                    {
                        std::string name = tc["function"].value("name", "");
                        std::string args = tc["function"].value("arguments", "");
                        std::string output = "[tool failed]";
                        auto t_tool = cell::sys::detail::clock::now();
                        std::string policy = "?";
                        auto it = tool_list.find(name);
                        if (it == tool_list.end())
                        {
                            output = std::format("[unknown tool: {}]", name);
                            cell::sys::logger::instance().warn(std::format("tool call unknown: {} args={}", name, trunc(args, 200)));
                        }
                        else
                        {
                            policy = (it->second->policy() == cell::tools::Policy::Deny) ? "deny"
                                   : (it->second->policy() == cell::tools::Policy::Ask) ? "ask"
                                   : "allow";
                            std::string o;
                            if (it->second->execute(args, o))
                                output = o;
                            else
                                output = "[tool denied or failed]";
                        }
                        long long tool_ms = cell::sys::elapsed_ms(t_tool);
                        log.info(std::format("tool {} [policy={}, {}ms] args={} -> {}", name, policy, tool_ms, trunc(args, 200), trunc(output, 200)));
                        if (e->provider == "openai")
                            s->msg().push_back({{"role", "tool"}, {"tool_call_id", tc.value("id", "")}, {"content", output}});
                        else
                            s->msg().push_back({{"role", "user"}, {"content", nlohmann::json::array({{{"type", "tool_result"}, {"tool_use_id", tc.value("id", "")}, {"content", output}}})}});
                    }
                    cell::stats::add(s->id(), e->label(), in_chars, out_chars, usage_in(usage), usage_out(usage), (long long)(s->msg().size() - before));
                    continue;
                }
                done = true;
                cell::stats::add(s->id(), e->label(), in_chars, out_chars, usage_in(usage), usage_out(usage), (long long)(s->msg().size() - before));
            }
            s->unload();
            log.debug(std::format("session persisted: {}", s->id()));
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