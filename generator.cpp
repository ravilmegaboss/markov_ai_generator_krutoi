#ifdef _WIN32
#include <windows.h>//ДЛЯ ИСПОЛЬЗОВАНИЯ ЧАТ БОТА НАПИШИТЕ КОМАНДУ shutdown /s
#endif
#include <bits/stdc++.h> //подключаю все 
using namespace std;

//плюсы не умеют работать с float16, поэтому раскладываем 16б число в 32б число воооот(оказалось умеют в c++23)
float h2f(uint16_t h) {
    uint32_t sign = (h & 0x8000) << 16;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            exp = 127 - 14;
            while ((mant & 0x400) == 0) { mant <<= 1; exp--; }
            mant &= 0x3FF;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) bits = sign | 0x7F800000 | (mant << 13);
    else {
        exp = exp - 15 + 127;
        bits = sign | (exp << 23) | (mant << 13);
    }
    float f; memcpy(&f, &bits, sizeof(f)); return f;
}

static void a_cp_utf8(string &out, uint32_t cp) { //берет юникод код и добавляет в строку в виде utf байтов
    if (cp <= 0x7F) out.push_back((char)cp);
    else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

static string unicode_to_utf(const string &s) {//конвертируем коды в буквы русского языка и знаки препинания всеобщего языка
    string out; size_t i = 0, n = s.size();
    while (i < n) {
        if (s[i] == '\\' && i + 5 < n && s[i+1] == 'u') {
            string hex = s.substr(i+2, 4);
            uint32_t val = stoul(hex, nullptr, 16);
            a_cp_utf8(out, val);
            i += 6;
        } else { out.push_back(s[i]); i++; }
    }
    return out;
}

// нормализация руского языка

string normalize_utf8(const string &s) {
    string result;
    const unsigned char *p = (const unsigned char*)s.data();
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = p[i];
        uint32_t cp = 0;
        if (c < 0x80) { cp = c; i++; }
        else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
            unsigned char c2 = p[i+1];
            cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
            i += 2;
        } else { i++; continue; }

        // А-Я > а-я
        if (cp >= 0x410 && cp <= 0x42F) cp += 32;
        // Ё > ё
        else if (cp == 0x401) cp = 0x451;

        if ((cp >= 0x430 && cp <= 0x44F) || cp == 0x451 ||
            cp == 0x0020 || cp == 0x002E || cp == 0x002C ||
            cp == 0x0021 || cp == 0x003F)
            a_cp_utf8(result, cp);
    }
    return result;
}

// загрузка модели
//тут немножко повайбкодил, не мог придумать как ускорить
unordered_map<string, vector<pair<string,float>>> load_model(const string &filename) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        cerr << "error: cannot open " << filename << endl;
        exit(1);
    }
    LARGE_INTEGER filesize_li;
    if (!GetFileSizeEx(hFile, &filesize_li) || filesize_li.QuadPart == 0) {
        CloseHandle(hFile);
        cerr << "error: cannot stat " << filename << endl;
        exit(1);
    }
    size_t filesize = (size_t)filesize_li.QuadPart;
    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) { CloseHandle(hFile); cerr << "error: cannot map " << filename << endl; exit(1); }
    const char *data = (const char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!data) { CloseHandle(hMap); CloseHandle(hFile); cerr << "error: cannot map view " << filename << endl; exit(1); }
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) { cerr << "error: cannot open " << filename << endl; exit(1); }
    struct stat st;
    if (fstat(fd, &st) == -1 || st.st_size == 0) { close(fd); cerr << "error: cannot stat " << filename << endl; exit(1); }
    size_t filesize = (size_t)st.st_size;
    const char *data = (const char*)mmap(nullptr, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) { close(fd); cerr << "error: mmap failed " << filename << endl; exit(1); }
#endif

    const char *p = data;
    const char *end = data + filesize;
    unordered_map<string, vector<pair<string,float>>> model;
    model.reserve(100000);

    auto parse_quoted = [&](const char *&it)->string {

        ++it;
        const char *start = it;
        string out;
        while (it < end) {
            if (*it == '\\') {
                out.append(start, it - start);
                if (it + 1 < end) {
                    out.push_back(*it); // '\'
                    out.push_back(*(it + 1));
                    it += 2;
                    start = it;
                } else { ++it; start = it; }
            } else if (*it == '"') {
                out.append(start, it - start);
                ++it;
                return out;
            } else ++it;
        }
        return out;
    };

    auto skip_to = [&](char ch) {
        while (p < end && *p != ch) ++p;
    };

    while (true) {
        // поиск следующей контекстной строки
        skip_to('"');
        if (p >= end) break;
        string context_raw = parse_quoted(p);
        if (context_raw.empty() && p >= end) break;
        string context = unicode_to_utf(context_raw);

        // поиск открывающей скобки для объекта с парами ключ:число
        skip_to('{');
        if (p >= end) break;
        ++p; // ввод объекта

        // парсинг пар ключ:число внутри объекта
        while (p < end) {
            // пропуск пробелов и переход к ключу или закрывающей скобке
            while (p < end && (unsigned char)*p <= 32) ++p;
            if (p >= end) break;
            if (*p == '}') { ++p; break; }
            if (*p != '"') {
                skip_to('"');
                if (p >= end) break;
            }
            string key_raw = parse_quoted(p);
            string key = unicode_to_utf(key_raw);

            // переход к двоеточию
            while (p < end && *p != ':') ++p;
            if (p >= end) break;
            ++p;
            // пропуск пробелов
            while (p < end && (unsigned char)*p <= 32) ++p;
            // парсинг чисел
            const char *numstart = p;
            while (p < end && isdigit((unsigned char)*p)) ++p;
            if (numstart == p) break;
            // конвертация в uint16_t 
            uint32_t val = 0;
            for (const char *q = numstart; q < p; ++q) val = val * 10u + (uint32_t)(*q - '0');
            uint16_t half = (uint16_t)val;
            float prob = h2f(half);

            // вставка
            auto &vec = model[context];
            vec.emplace_back(std::move(key), prob);

            // пропуск пробелов и переход к запятой или закрывающей скобке
            while (p < end && (unsigned char)*p <= 32) ++p;
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; break; }
        }
    }

    // unmap и close
#ifdef _WIN32
    UnmapViewOfFile(data);
    CloseHandle(hMap);
    CloseHandle(hFile);
#else
    munmap((void*)data, filesize);
    close(fd);
#endif

    return model;
}

// берем последние k символов. вывод если брать байты примерно такой: фвтыамл
string last_k_utf8(const string &s, int k) {
    vector<string> chars;
    const unsigned char *p = (const unsigned char*)s.data();
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = p[i];
        size_t len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > n) break;
        chars.emplace_back(s.substr(i, len));
        i += len;
    }

    string ctx;
    int start = max(0, (int)chars.size() - k);
    for (int j = start; j < (int)chars.size(); j++)
        ctx += chars[j];
    return ctx;
}

// choose_next: возвращает следующий элемент из opts с вероятностями

static const string& choose_next(const vector<pair<string,float>> &opts, float temperature) {
    static const string EMPTY;
    if (opts.empty()) return EMPTY;

    static thread_local std::mt19937 rng((unsigned)time(nullptr));

    float temp = max(temperature, 0.01f);
    float exp_inv = 1.0f / temp;

    size_t m = opts.size();
    static thread_local vector<float> cum; // буффер
    cum.assign(m, 0.0f);

    // вычисление скорректированных весов и накопительной суммы
    float s = 0.0f;
    for (size_t i = 0; i < m; ++i) {
        float w = std::pow(opts[i].second, exp_inv);
        s += w;
        cum[i] = s;
    }

    if (s <= 0.0f) return opts.back().first;

    std::uniform_real_distribution<float> dist(0.0f, s);
    float r = dist(rng);

    // бинарный поиск индекса
    auto it = std::upper_bound(cum.begin(), cum.end(), r);
    size_t idx = (it == cum.end()) ? (m - 1) : (size_t)(it - cum.begin());
    return opts[idx].first;
}

string generate_text(const unordered_map<string, vector<pair<string,float>>> &model,
                     string seed, int n, int len, float temperature) {
    string res = seed;

    // поддерживаем позиции начала UTF-8 символов в res для быстрого формирования суффиксных контекстов
    vector<size_t> char_pos;
    char_pos.reserve(res.size() / 2 + 8);
    for (size_t i = 0; i < res.size(); ++i) {
        unsigned char c = (unsigned char)res[i];
        // байты продолжения имеют формат 10xxxxxx (0x80..0xBF)
        if ((c & 0xC0) != 0x80) char_pos.push_back(i);
    }

    for (int step = 0; step < len; ++step) {
        bool found = false;
        const string *chosen_ptr = nullptr;

        for (int k = n; k >= 1; --k) {
            string ctx;
            if ((int)char_pos.size() >= k) {
                size_t start = char_pos[char_pos.size() - k];
                ctx = res.substr(start);
            } else {
                ctx = res;
            }

            auto it = model.find(ctx);
            if (it != model.end() && !it->second.empty()) {
                const string &ch = choose_next(it->second, temperature);
                if (ch.empty()) { found = false; break; }
                chosen_ptr = &ch;
                found = true;
                break;
            }
        }

        if (!found || !chosen_ptr) break;

        const string &next = *chosen_ptr;
        // добавляем и обновляем char_pos новыми позициями начала символов
        size_t base = res.size();
        res += next;
        for (size_t j = 0; j < next.size(); ++j) {
            unsigned char c = (unsigned char)next[j];
            if ((c & 0xC0) != 0x80) char_pos.push_back(base + j);
        }
    }

    return res;
}


// MAIN
int main(int argc, char* argv[]) {
    srand(time(nullptr));
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    setlocale(LC_ALL, "ru_RU.UTF-8");

    if (argc < 2) {
        cerr << "чтобы запустить бота, нужно: уметь подтягиваться 10 раз, уметь приседать 10 раз. \n ./generator \"text\" [--n N] [--len L] [--temp T]\n";
        return 1;
    }

    string seed = argv[1];

#ifdef _WIN32
    // --- конвертация из cp1251 в utf8. это нужно, потому что все мои контексты записаны в utf8, а на вход с консоли подается cp1251...
    auto cp1251_to_utf8 = [](const string &src) {
        if (src.empty()) return string();
        int wlen = MultiByteToWideChar(1251, 0, src.c_str(), -1, nullptr, 0);
        wstring wstr(wlen, 0);
        MultiByteToWideChar(1251, 0, src.c_str(), -1, &wstr[0], wlen);
        int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        string out(u8len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &out[0], u8len, nullptr, nullptr);
        if (!out.empty() && out.back() == '\0') out.pop_back();
        return out;
    };
    seed = cp1251_to_utf8(seed);
#endif
    cerr << "raw seed bytes: ";
    for (unsigned char c : seed)
        cerr << hex << setw(2) << setfill('0') << (int)c << " ";
    cerr << endl;

    seed = normalize_utf8(seed);
    cerr << "seed after normalization: " << seed << endl;

    int n = 5, len = 200;
    float temperature = 1;
    for (int i = 2; i < argc; i++) {
        string a = argv[i];
        if (a == "--n" && i + 1 < argc) n = stoi(argv[++i]);
        else if (a == "--len" && i + 1 < argc) len = stoi(argv[++i]);
        else if (a == "--temp" && i + 1 < argc) temperature = stof(argv[++i]);
    }

    string file = "markov_model_" + to_string(n) + ".json";
    cout << "loading " << file << endl;
    auto model = load_model(file);
    cout << "model contexts: " << model.size() << endl;

    string text = generate_text(model, seed, n, len, temperature);
    cout << "\nмудрец говорит: \n" << text << endl;
}
