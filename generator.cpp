#ifdef _WIN32
#include <windows.h>//ДЛЯ ИСПОЛЬЗОВАНИЯ ЧАТ БОТА НАПИШИТЕ КОМАНДУ shutdown /s
#endif
#include <bits/stdc++.h> //подключаю все 
using namespace std;

// ===================================================
// float16 > float32
// ===================================================
//плюсы не умеют работать с float16, поэтому раскладываем 16б число в 32б число воооот
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

// ===================================================
// utf8
// ===================================================

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

// ===================================================
// нормализация нашего родного руского языка
// ===================================================
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
// ===================================================
// загрузка модели
// ===================================================
//по своей сути сердце модели. парсинг делается ручками билла гейтса при вызове))) чтобы было быстрее
unordered_map<string, vector<pair<string,float>>> load_model(const string &filename) {
    ifstream in(filename, ios::binary);
    if (!in) {
        cerr << "error: cannot open " << filename << endl;
        exit(1);
    }
    string json((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    unordered_map<string, vector<pair<string,float>>> model;
    size_t pos = 0;

    while (true) {
        size_t q = json.find('"', pos); if (q == string::npos) break;
        size_t q2 = json.find('"', q+1); if (q2 == string::npos) break;
        string context = unicode_to_utf(json.substr(q+1, q2-q-1));

        size_t b1 = json.find('{', q2); if (b1 == string::npos) break;
        size_t b2 = json.find('}', b1); if (b2 == string::npos) break;
        string inside = json.substr(b1+1, b2-b1-1);

        size_t p = 0;
        while (true) {
            size_t k1 = inside.find('"', p); if (k1 == string::npos) break;
            size_t k2 = inside.find('"', k1+1); if (k2 == string::npos) break;
            string key = unicode_to_utf(inside.substr(k1+1, k2-k1-1));

            size_t colon = inside.find(':', k2);
            if (colon == string::npos) break;
            size_t numstart = colon+1;
            while (numstart < inside.size() && isspace((unsigned char)inside[numstart])) numstart++;
            size_t numend = numstart;
            while (numend < inside.size() && isdigit((unsigned char)inside[numend])) numend++;
            if (numend == numstart) break;

            uint16_t half = (uint16_t)stoul(inside.substr(numstart, numend-numstart));
            float prob = h2f(half);
            model[context].push_back({key, prob});

            size_t comma = inside.find(',', numend);
            if (comma == string::npos) break;
            p = comma + 1;
        }

        pos = b2 + 1;
    }

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

// ===================================================
// ^_^ Генерация ^_^
// ===================================================
string choose_next(const vector<pair<string,float>> &opts, float temperature) {
    if (opts.empty()) return "";
    vector<float> adjusted;
    adjusted.reserve(opts.size());
    float sum = 0;
    for (auto &p : opts) {
        float val = pow(p.second, 1.0f / max(temperature, 0.01f));
        adjusted.push_back(val);
        sum += val;
    }

    float r = ((float)rand() / RAND_MAX) * sum;
    float cum = 0;
    for (size_t i = 0; i < opts.size(); i++) {
        cum += adjusted[i];
        if (r <= cum) return opts[i].first;
    }
    return opts.back().first;
}
string generate_text(const unordered_map<string, vector<pair<string,float>>> &model,
                     string seed, int n, int len, float temperature) {
    string res = seed;
    for (int i = 0; i < len; i++) {
        string next;
        bool found = false;
        for (int k = n; k >= 1; k--) {
            string ctx = last_k_utf8(res, k);
            auto it = model.find(ctx);
            if (it != model.end() && !it->second.empty()) {
                next = choose_next(it->second, temperature);
                found = true;
                break;
            }
        }
        if (!found) break;
        res += next;
    }
    return res;
}

// ===================================================
// MAIN
// ====================================================
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
    float temperature = 1.0;
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
