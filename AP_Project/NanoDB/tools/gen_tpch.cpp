// gen_tpch: generate a small TPC-H-shaped dataset on disk.
//
// Output files (in --out directory):
//   customer.tbl   pipe-delimited, 8 columns
//   orders.tbl     pipe-delimited, 9 columns
//   lineitem.tbl   pipe-delimited, 16 columns
//
// Default scale: 20000 customers, 30000 orders, 50000 line items
// (matching the spec's "100,000 records across these tables").

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

unsigned long long lcg_state = 1469598103934665603ULL;

unsigned long long rng() {
    lcg_state ^= lcg_state >> 12;
    lcg_state ^= lcg_state << 25;
    lcg_state ^= lcg_state >> 27;
    return lcg_state * 2685821657736338717ULL;
}

int randInt(int lo, int hi) {
    if (hi <= lo) return lo;
    unsigned long long r = rng();
    return lo + (int)(r % (unsigned long long)(hi - lo + 1));
}

double randDouble(double lo, double hi) {
    double t = (double)(rng() % 1000000ULL) / 1000000.0;
    return lo + t * (hi - lo);
}

const char* randSegment() {
    static const char* segs[] = {"BUILDING","AUTOMOBILE","MACHINERY","HOUSEHOLD","FURNITURE"};
    return segs[randInt(0, 4)];
}

const char* randStatus() {
    static const char* st[] = {"O","F","P"};
    return st[randInt(0, 2)];
}

const char* randPriority() {
    static const char* pri[] = {"1-URGENT","2-HIGH","3-MEDIUM","4-NOT SPECIFIED","5-LOW"};
    return pri[randInt(0, 4)];
}

const char* randFlag() {
    static const char* fl[] = {"N","R","A"};
    return fl[randInt(0, 2)];
}

const char* randMode() {
    static const char* m[] = {"AIR","RAIL","SHIP","TRUCK","MAIL","FOB","REG AIR"};
    return m[randInt(0, 6)];
}

void randName(char* buf, int n, const char* prefix, int id) {
    std::snprintf(buf, (size_t)n, "%s#%09d", prefix, id);
}

void randPhone(char* buf, int n) {
    std::snprintf(buf, (size_t)n, "%02d-%03d-%03d-%04d",
                  randInt(10, 99), randInt(100, 999),
                  randInt(100, 999), randInt(1000, 9999));
}

void randDate(char* buf, int n) {
    int y = randInt(1992, 1998);
    int m = randInt(1, 12);
    int d = randInt(1, 28);
    std::snprintf(buf, (size_t)n, "%04d-%02d-%02d", y, m, d);
}

void writeCustomers(const char* path, int n) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return; }
    char name[64], phone[32];
    for (int i = 1; i <= n; ++i) {
        randName(name, sizeof(name), "Customer", i);
        randPhone(phone, sizeof(phone));
        const char* seg = randSegment();
        double bal = randDouble(-999.0, 9999.99);
        int nation = randInt(0, 24);
        std::fprintf(f, "%d|%s|Addr#%d|%d|%s|%.2f|%s|comment_%d|\n",
                     i, name, randInt(1, 99999), nation, phone, bal, seg, randInt(0, 9999));
    }
    std::fclose(f);
}

void writeOrders(const char* path, int n, int customerN) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return; }
    char clerk[32], date[16];
    for (int i = 1; i <= n; ++i) {
        int ck = randInt(1, customerN);
        const char* st = randStatus();
        double tp = randDouble(900.0, 500000.0);
        randDate(date, sizeof(date));
        const char* pri = randPriority();
        std::snprintf(clerk, sizeof(clerk), "Clerk#%09d", randInt(1, 1000));
        int sp = 0;
        std::fprintf(f, "%d|%d|%s|%.2f|%s|%s|%s|%d|comment_%d|\n",
                     i, ck, st, tp, date, pri, clerk, sp, randInt(0, 9999));
    }
    std::fclose(f);
}

void writeLineItems(const char* path, int n, int orderN) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return; }
    char ship[16], commit[16], receipt[16];
    for (int i = 1; i <= n; ++i) {
        int ok = randInt(1, orderN);
        int pk = randInt(1, 200000);
        int sk = randInt(1, 10000);
        int ln = randInt(1, 7);
        double q = randDouble(1.0, 50.0);
        double ep = randDouble(900.0, 90000.0);
        double disc = randDouble(0.0, 0.10);
        double tax = randDouble(0.0, 0.08);
        const char* rf = randFlag();
        const char* ls = randStatus();
        randDate(ship, sizeof(ship));
        randDate(commit, sizeof(commit));
        randDate(receipt, sizeof(receipt));
        const char* mode = randMode();
        std::fprintf(f, "%d|%d|%d|%d|%.2f|%.2f|%.4f|%.4f|%s|%s|%s|%s|%s|DELIVER|%s|comment_%d|\n",
                     ok, pk, sk, ln, q, ep, disc, tax, rf, ls, ship, commit, receipt, mode, randInt(0, 9999));
    }
    std::fclose(f);
}

} // anon

int main(int argc, char** argv) {
    int customers = 20000, orders = 30000, lineitems = 50000;
    const char* out = "data";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) { out = argv[++i]; }
        else if (std::strcmp(argv[i], "--customers") == 0 && i + 1 < argc) { customers = std::atoi(argv[++i]); }
        else if (std::strcmp(argv[i], "--orders") == 0 && i + 1 < argc) { orders = std::atoi(argv[++i]); }
        else if (std::strcmp(argv[i], "--lineitems") == 0 && i + 1 < argc) { lineitems = std::atoi(argv[++i]); }
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { lcg_state = (unsigned long long)std::atoll(argv[++i]) | 1ULL; }
    }
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/customer.tbl", out);
    writeCustomers(path, customers);
    std::snprintf(path, sizeof(path), "%s/orders.tbl", out);
    writeOrders(path, orders, customers);
    std::snprintf(path, sizeof(path), "%s/lineitem.tbl", out);
    writeLineItems(path, lineitems, orders);
    std::printf("Generated TPC-H subset: %d customers, %d orders, %d line items into %s\n",
                customers, orders, lineitems, out);
    return 0;
}
