#include <bits/stdc++.h>
using namespace std;

// ==========================================
// конфиг????
// ==========================================
const string CORPUS_FILE = "corpus.txt";
const size_t SAVE_INTERVAL = 1'000'000; // сохраняем каждый миллион символов

// ==========================================
// float 32 > float16
// ==========================================
uint16_t float_to_half(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000;
    uint32_t mantissa = bits & 0x007FFFFF;
    int32_t exp = ((bits >> 23) & 0xFF) - 127 + 15;
    if (exp <= 0) return sign;
    if (exp >= 31) return sign | 0x7C00;
    return sign | (exp << 10) | (mantissa >> 13);
}

// ==========================================
// utf8
// ==========================================
static void append_codepoint_utf8(string &out, uint32_t cp) {
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

static void for_each_codepoint(const string &s, const function<void(uint32_t)> &visitor) {
    const unsigned char *p = (const unsigned char*)s.data();
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = p[i];
        uint32_t cp = 0;
        if (c < 0x80) { cp = c; i += 1; }
        else if ((c >> 5) == 0x6) { cp = ((p[i]&0x1F)<<6)|(p[i+1]&0x3F); i+=2; }
        else if ((c >> 4) == 0xE) { cp=((p[i]&0x0F)<<12)|((p[i+1]&0x3F)<<6)|(p[i+2]&0x3F); i+=3; }
        else if ((c >> 3) == 0x1E) { cp=((p[i]&0x07)<<18)|((p[i+1]&0x3F)<<12)|((p[i+2]&0x3F)<<6)|(p[i+3]&0x3F); i+=4; }
        else { i++; continue; }
        visitor(cp);
    }
}

static string encode_as_unicode_escape(const string &utf8str) {
    ostringstream oss;
    oss << hex << setfill('0');
    for_each_codepoint(utf8str, [&](uint32_t cp){
        if (cp <= 0xFFFF) oss << "\\u" << setw(4) << (int)cp;
        else {
            uint32_t U = cp - 0x10000;
            uint16_t hi = 0xD800 | ((U >> 10) & 0x3FF);
            uint16_t lo = 0xDC00 | (U & 0x3FF);
            oss << "\\u" << setw(4) << (int)hi << "\\u" << setw(4) << (int)lo;
        }
    });
    oss << dec;
    return oss.str();
}

// ==========================================
// запись в json
// ==========================================
void save_partial_json(const string& filename,
    const unordered_map<string, unordered_map<string, uint64_t>>& data)
{
    ofstream out(filename, ios::binary);
    out << "{\n";
    bool first_outer = true;
    for (auto &outer : data) {
        if (!first_outer) out << ",\n";
        first_outer = false;
        out << "\"" << encode_as_unicode_escape(outer.first) << "\": {";
        bool first_inner = true;
        for (auto &kv : outer.second) {
            if (!first_inner) out << ", ";
            first_inner = false;
            out << "\"" << encode_as_unicode_escape(kv.first) << "\": " << kv.second;
        }
        out << "}";
    }
    out << "\n}\n";
}

void save_final_json(const string& filename,
    const unordered_map<string, unordered_map<string, uint64_t>>& data)
{
    ofstream out(filename, ios::binary);
    out << "{\n";
    bool first_outer = true;
    for (auto &outer : data) {
        if (!first_outer) out << ",\n";
        first_outer = false;
        out << "\"" << encode_as_unicode_escape(outer.first) << "\": {";
        uint64_t total = 0;
        for (auto &kv : outer.second) total += kv.second;
        bool first_inner = true;
        for (auto &kv : outer.second) {
            if (!first_inner) out << ", ";
            first_inner = false;
            float prob = total ? (float)kv.second / total : 0.0f;
            uint16_t half = float_to_half(prob);
            out << "\"" << encode_as_unicode_escape(kv.first) << "\": " << half;
        }
        out << "}";
    }
    out << "\n}\n";
}

// ==========================================
// обработка
// ==========================================
void process_n(int n, bool resume) {
    string progress_file = "markov_progress_" + to_string(n) + ".txt";
    string partial_file  = "markov_model_" + to_string(n) + "_partial.json";
    string final_file    = "markov_model_" + to_string(n) + ".json";

    unordered_map<string, unordered_map<string, uint64_t>> stats;
    ifstream in(CORPUS_FILE, ios::binary);
    if (!in) { cerr << "No corpus.txt\n"; return; }

    size_t processed_bytes = 0;
    if (resume) {
        ifstream p(progress_file);
        if (p) p >> processed_bytes;
        in.seekg(processed_bytes, ios::beg);
        cout << "[RESUME] from byte " << processed_bytes << "\n";
    }

    deque<string> buf;
    size_t total_bytes = processed_bytes, processed_chars = 0;
    unsigned char c1;
    while (in.read((char*)&c1,1)) {
        char32_t cp = 0;
        if (c1 < 0x80) cp = c1;
        else if ((c1>>5)==0x6) {
            unsigned char c2; if(!in.read((char*)&c2,1)) break;
            cp = ((c1&0x1F)<<6)|(c2&0x3F); total_bytes++;
        } else { unsigned char skip; in.read((char*)&skip,1); total_bytes++; continue; }
        total_bytes++;

        if (cp>=U'А'&&cp<=U'Я') cp += 32;
        else if (cp==U'Ё') cp=U'ё';
        if (!((cp>=U'а'&&cp<=U'я')||cp==U'ё'||cp==U'.'||cp==U','||cp==U' '||cp==U'!'||cp==U'?')) continue;

        string sym; append_codepoint_utf8(sym, cp);
        if (buf.size()< (size_t)n) { buf.push_back(sym); continue; }
        string ctx; for(auto&s:buf) ctx+=s;
        stats[ctx][sym]++;
        buf.pop_front(); buf.push_back(sym);
        processed_chars++;
        if (processed_chars % SAVE_INTERVAL==0) {
            cout << "[SAVE] " << processed_chars << " chars\n";
            save_partial_json(partial_file, stats);
            ofstream p(progress_file); p<<total_bytes;
        }
    }
    cout << "[FINAL] n="<<n<<", chars="<<processed_chars<<"\n";
    save_final_json(final_file, stats);
    ofstream p(progress_file); p<<total_bytes;
}

// ==========================================
// main
// ==========================================
int main(int argc,char*argv[]){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    int max_n=0; bool resume=false;
    for(int i=1;i<argc;i++){
        string arg=argv[i];
        if(arg=="--n"&&i+1<argc) max_n=stoi(argv[++i]);
        else if(arg=="--resume") resume=true;
    }
    if(max_n<1||max_n>16){ cerr<<"usage: markov --n <1-16> [--resume]\n"; return 1; }
    for(int n=1;n<=max_n;n++){
        cout<<"==== building n="<<n<<" ====\n";
        process_n(n,resume);
    }
    cout<<"done.\n";
}
 