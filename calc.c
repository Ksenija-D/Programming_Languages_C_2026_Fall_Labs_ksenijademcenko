// Ksenija Demchenko 251RDC083
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Claude AI, used for selected implementation suggestions,
// reviewing some approaches, and helping formulate code comments based on my
// explanations

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Used in the output file and folder names.
#define STUDENT_NAME "Ksenija"
#define STUDENT_LASTNAME "Demchenko"
#define STUDENT_ID "251RDC083"

// If memory runs out I just stop the program.
static void* xrealloc(void* p, size_t n) {
  void* q = realloc(p, n);
  if (q == NULL) {
    fprintf(stderr, "calc: out of memory\n");
    exit(1);
  }
  return q;
}

// Buffer that grows, used for the input file and for the output.
// AI: the idea to double the size came from Claude. If it grew by a fixed
// amount big files would be copied again and again and it would be slow.
typedef struct {
  char* data;
  size_t len;
  size_t cap;
} Buf;

static void buf_reserve(Buf* b, size_t extra) {
  if (b->len + extra <= b->cap) return;
  size_t cap = b->cap ? b->cap : 4096;
  while (cap < b->len + extra) cap *= 2;
  b->data = xrealloc(b->data, cap);
  b->cap = cap;
}

static void buf_append(Buf* b, const char* s, size_t n) {
  buf_reserve(b, n);
  memcpy(b->data + b->len, s, n);
  b->len += n;
}

// Reads the whole file into memory at once. I keep one extra byte for '\0'
// at the end so the tokenizer can look one char ahead safely.
static int read_file(const char* path, Buf* b) {
  FILE* f = fopen(path, "rb");
  if (f == NULL) return 0;
  b->len = 0;
  for (;;) {
    buf_reserve(b, 65536 + 1);
    size_t n = fread(b->data + b->len, 1, b->cap - b->len - 1, f);
    b->len += n;
    if (n == 0) break;
  }
  int ok = !ferror(f);
  fclose(f);
  b->data[b->len] = '\0';
  return ok;
}

// A number is either an integer or a double, is_float says which one.
typedef struct {
  int is_float;
  long long i;
  double d;
} Value;

static double as_double(Value v) { return v.is_float ? v.d : (double)v.i; }

// AI helped me consider how signed integer overflow can be avoided when
// implementing integer arithmetic.
static long long wrap_add(long long a, long long b) {
  return (long long)((unsigned long long)a + (unsigned long long)b);
}
static long long wrap_sub(long long a, long long b) {
  return (long long)((unsigned long long)a - (unsigned long long)b);
}
static long long wrap_mul(long long a, long long b) {
  return (long long)((unsigned long long)a * (unsigned long long)b);
}

// Fast power (squaring), so big exponents don't take forever. e is never
// negative here.
static long long int_pow(long long base, long long e) {
  unsigned long long result = 1, b = (unsigned long long)base;
  while (e > 0) {
    if (e & 1) result *= b;
    b *= b;
    e >>= 1;
  }
  return (long long)result;
}

enum {
  T_NUM,
  T_PLUS,
  T_MINUS,
  T_STAR,
  T_SLASH,
  T_POW,
  T_LPAREN,
  T_RPAREN,
  T_EOL,
  T_BAD
};

// One operand of a ** chain, see parse_power.
typedef struct {
  Value v;
  size_t start;  // where the operand starts
  size_t op;     // where the ** before it is
} PowItem;

// Positions: the whole file is one array and pos is just an index in it.
// A token's position is the index where it starts, so the 1-based position
// for the error is index + 1. Newlines and comment lines are counted
// automatically because they are in the array too.
typedef struct {
  char* src;
  size_t len;
  size_t pos;      // next char to read
  int tok;         // current token
  size_t tok_pos;  // where the current token starts
  Value num;       // value if the token is a number
  size_t err_pos;  // error position (1-based)

  PowItem* pow;  // stack for ** chains
  size_t pow_len, pow_cap;
} Parser;

// Saves the error position and returns 0. After an error every function
// returns right away, so the first error is the one that stays.
static int fail(Parser* p, size_t at) {
  p->err_pos = at + 1;
  return 0;
}

static void lex_number(Parser* p) {
  char* s = p->src;
  size_t start = p->pos;
  unsigned long long acc = 0;
  while (isdigit((unsigned char)s[p->pos]))
    acc = acc * 10 + (unsigned long long)(s[p->pos++] - '0');

  if (s[p->pos] != '.') {
    p->num.is_float = 0;
    p->num.i = (long long)acc;
    return;
  }
  // After '.' there must be a digit, otherwise (like "5.") the error is at the
  // '.'.
  if (!isdigit((unsigned char)s[p->pos + 1])) {
    p->tok = T_BAD;
    p->tok_pos = p->pos;
    return;
  }
  p->pos++;
  while (isdigit((unsigned char)s[p->pos])) p->pos++;
  // strtod would also read "1.5e3", so I put '\0' after the number for a
  // moment. Then the 'e' becomes the next token and gives an error at the 'e'.
  char saved = s[p->pos];
  s[p->pos] = '\0';
  p->num.is_float = 1;
  p->num.d = strtod(s + start, NULL);
  s[p->pos] = saved;
}

// Skips spaces and tabs ('\r' too, for Windows files) and reads one token.
// The '\n' is not skipped, it becomes T_EOL. At the end of the file T_EOL is
// at index len, so the error there is file length + 1 like the task says.
static void next_token(Parser* p) {
  char* s = p->src;
  while (p->pos < p->len &&
         (s[p->pos] == ' ' || s[p->pos] == '\t' || s[p->pos] == '\r'))
    p->pos++;
  p->tok_pos = p->pos;
  if (p->pos >= p->len || s[p->pos] == '\n') {
    p->tok = T_EOL;
    return;
  }
  char c = s[p->pos];
  if (isdigit((unsigned char)c)) {
    p->tok = T_NUM;
    lex_number(p);
    return;
  }
  switch (c) {
    case '+':
      p->tok = T_PLUS;
      break;
    case '-':
      p->tok = T_MINUS;
      break;
    case '/':
      p->tok = T_SLASH;
      break;
    case '(':
      p->tok = T_LPAREN;
      break;
    case ')':
      p->tok = T_RPAREN;
      break;
    case '*':
      if (s[p->pos + 1] == '*') {
        p->tok = T_POW;
        p->pos += 2;
        return;
      }
      p->tok = T_STAR;
      break;
    default:
      // Invalid char (also '#' in the middle of a line). The parser fails
      // here so the error is at this char.
      p->tok = T_BAD;
      return;
  }
  p->pos++;
}

// If both numbers are integers I use long long, so 7 / 2 = 3 like in C.
// If one of them is a float both become double. If the double result is inf or
// nan the error is at the operator because the operator made it.
static int apply(Parser* p, int op, Value a, Value b, size_t op_pos,
                 size_t rhs_pos, Value* out) {
  // Division by zero is at the right operand because the problem is the
  // divisor.
  if (op == T_SLASH && (b.is_float ? b.d == 0.0 : b.i == 0))
    return fail(p, rhs_pos);

  if (!a.is_float && !b.is_float) {
    out->is_float = 0;
    switch (op) {
      case T_PLUS:
        out->i = wrap_add(a.i, b.i);
        break;
      case T_MINUS:
        out->i = wrap_sub(a.i, b.i);
        break;
      case T_STAR:
        out->i = wrap_mul(a.i, b.i);
        break;
      default:
        // x / -1 is done as -x, because LLONG_MIN / -1 is undefined.
        out->i = (b.i == -1) ? wrap_sub(0, a.i) : a.i / b.i;
        break;
    }
    return 1;
  }

  double x = as_double(a), y = as_double(b), r;
  switch (op) {
    case T_PLUS:
      r = x + y;
      break;
    case T_MINUS:
      r = x - y;
      break;
    case T_STAR:
      r = x * y;
      break;
    default:
      r = x / y;
      break;
  }
  if (!isfinite(r)) return fail(p, op_pos);
  out->is_float = 1;
  out->d = r;
  return 1;
}

static int apply_pow(Parser* p, Value a, Value b, size_t op_pos, size_t exp_pos,
                     Value* out) {
  if (!a.is_float && !b.is_float) {
    // An integer can't hold 2 ** -1, so it's an error at the exponent.
    if (b.i < 0) return fail(p, exp_pos);
    out->is_float = 0;
    out->i = int_pow(a.i, b.i);
    return 1;
  }
  // With floats pow() is used, inf or nan (like (0 - 8) ** 0.5) is an error at
  // the **.
  double r = pow(as_double(a), as_double(b));
  if (!isfinite(r)) return fail(p, op_pos);
  out->is_float = 1;
  out->d = r;
  return 1;
}

// Precedence works because every level gets its operands from the level below.
// So * and / are done before expr sees + or -, and ** before * and /.expr and
// term use a loop and add each operand to the result right away,that's why 10 -
// 3 - 2 is (10 - 3) - 2. Errors: the parser checks the current token, and if
// it's not allowed there, the error is at the start of that token. For example
// in "7 + * 3" a number is expected but it sees '*', and in "(2 + 3" it expects
// ')' but sees the end of line.

static int parse_expr(Parser* p, Value* out);

static int parse_primary(Parser* p, Value* out) {
  if (p->tok == T_NUM) {
    *out = p->num;
    next_token(p);
    return 1;
  }
  if (p->tok == T_LPAREN) {
    next_token(p);
    if (!parse_expr(p, out)) return 0;
    if (p->tok != T_RPAREN) return fail(p, p->tok_pos);
    next_token(p);
    return 1;
  }
  // No unary minus, so "-5" also ends up here.
  return fail(p, p->tok_pos);
}

static void pow_push(Parser* p, Value v, size_t start, size_t op) {
  if (p->pow_len == p->pow_cap) {
    p->pow_cap = p->pow_cap ? p->pow_cap * 2 : 64;
    p->pow = xrealloc(p->pow, p->pow_cap * sizeof *p->pow);
  }
  p->pow[p->pow_len].v = v;
  p->pow[p->pow_len].start = start;
  p->pow[p->pow_len].op = op;
  p->pow_len++;
}

// ** is right-associative: 2 ** 3 ** 2 is 2 ** (3 ** 2) = 512.
// AI: Claude suggested doing it without recursion, because a very long
// line like "2 ** 1 ** 1 ** ..." could overflow the stack. So I collect all
// operands first and then calculate from the right end. The result is the
// same as with recursion. Nested chains in brackets use the stack above
// mine and put pow_len back when they are done.
static int parse_power(Parser* p, Value* out) {
  size_t base = p->pow_len;
  size_t start = p->tok_pos;
  Value v;
  if (!parse_primary(p, &v)) goto error;
  pow_push(p, v, start, 0);

  while (p->tok == T_POW) {
    size_t op = p->tok_pos;
    next_token(p);
    start = p->tok_pos;
    if (!parse_primary(p, &v)) goto error;
    pow_push(p, v, start, op);
  }

  // From right to left: r is the exponent for the item before it.
  Value r = p->pow[p->pow_len - 1].v;
  for (size_t k = p->pow_len - 1; k > base; k--) {
    if (!apply_pow(p, p->pow[k - 1].v, r, p->pow[k].op, p->pow[k].start, &r))
      goto error;
  }
  p->pow_len = base;
  *out = r;
  return 1;

error:
  p->pow_len = base;
  return 0;
}

static int parse_term(Parser* p, Value* out) {
  if (!parse_power(p, out)) return 0;
  while (p->tok == T_STAR || p->tok == T_SLASH) {
    int op = p->tok;
    size_t op_pos = p->tok_pos;
    next_token(p);
    size_t rhs_pos = p->tok_pos;  // needed for division by zero
    Value rhs;
    if (!parse_power(p, &rhs)) return 0;
    if (!apply(p, op, *out, rhs, op_pos, rhs_pos, out)) return 0;
  }
  return 1;
}

static int parse_expr(Parser* p, Value* out) {
  if (!parse_term(p, out)) return 0;
  while (p->tok == T_PLUS || p->tok == T_MINUS) {
    int op = p->tok;
    size_t op_pos = p->tok_pos;
    next_token(p);
    size_t rhs_pos = p->tok_pos;
    Value rhs;
    if (!parse_term(p, &rhs)) return 0;
    if (!apply(p, op, *out, rhs, op_pos, rhs_pos, out)) return 0;
  }
  return 1;
}

// Calculates all lines of the file and writes the text for the output file into
// out. Big files: the file is read once and the parser works directly in that
// buffer, lines are never copied. Results go into one buffer and are written at
// the end, so if there is an error I can just throw them away.
static void evaluate(char* src, size_t len, Buf* out) {
  Parser p = {0};
  p.src = src;
  p.len = len;
  out->len = 0;

  size_t line = 0;  // start of the current line
  while (line < len) {
    // Skip blank lines and lines where the first non-blank char is '#'.
    size_t q = line;
    while (q < len && (src[q] == ' ' || src[q] == '\t' || src[q] == '\r')) q++;
    if (q >= len || src[q] == '\n') {
      line = q + 1;
      continue;
    }
    if (src[q] == '#') {
      while (q < len && src[q] != '\n') q++;
      line = q + 1;
      continue;
    }

    p.pos = line;
    next_token(&p);
    Value v;
    int ok = parse_expr(&p, &v);
    // After the expression the line must end, otherwise (like an extra ')')
    // it's an error.
    if (ok && p.tok != T_EOL) ok = fail(&p, p.tok_pos);
    if (!ok) {
      char msg[64];
      int n = snprintf(msg, sizeof msg, "ERROR:%zu\n", p.err_pos);
      out->len = 0;  // only the error goes to the file
      buf_append(out, msg, (size_t)n);
      break;
    }

    char num[64];
    int n = v.is_float ? snprintf(num, sizeof num, "%.15g\n", v.d)
                       : snprintf(num, sizeof num, "%lld\n", v.i);
    buf_append(out, num, (size_t)n);
    line = p.tok_pos + 1;  // T_EOL is on the '\n', next line starts after it
  }
  free(p.pow);
}

// Makes "a/b", without a double slash.
static char* join_path(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  int slash = (la > 0 && a[la - 1] != '/');
  char* r = xrealloc(NULL, la + (size_t)slash + lb + 1);
  memcpy(r, a, la);
  if (slash) r[la] = '/';
  memcpy(r + la + slash, b, lb + 1);
  return r;
}

// Last part of the path ("labs/" gives "labs"), optionally without ".txt".
static char* base_name(const char* path, int strip_txt) {
  size_t end = strlen(path);
  while (end > 1 && path[end - 1] == '/') end--;
  size_t start = end;
  while (start > 0 && path[start - 1] != '/') start--;
  size_t n = end - start;
  if (strip_txt && n > 4 && memcmp(path + end - 4, ".txt", 4) == 0) n -= 4;
  char* r = xrealloc(NULL, n + 1);
  memcpy(r, path + start, n);
  r[n] = '\0';
  return r;
}

static int has_txt_suffix(const char* name) {
  size_t n = strlen(name);
  return n > 4 && strcmp(name + n - 4, ".txt") == 0;
}

// Creates the output folder. If it already exists that's fine.
static int ensure_dir(const char* dir) {
  if (mkdir(dir, 0777) == 0) return 1;
  int err = errno;
  struct stat st;
  if (err == EEXIST && stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
  fprintf(stderr, "calc: cannot create output folder '%s': %s\n", dir,
          err == EEXIST ? "exists and is not a folder" : strerror(err));
  return 0;
}

// Calculates one file and writes the result into outdir.
// The buffers come from outside so with -d all files use the same memory.
static int process_file(const char* in_path, const char* outdir, Buf* in,
                        Buf* out) {
  if (!read_file(in_path, in)) {
    fprintf(stderr, "calc: cannot read input file '%s': %s\n", in_path,
            strerror(errno));
    return 0;
  }
  evaluate(in->data, in->len, out);

  char* base = base_name(in_path, 1);
  const char* suffix =
      "_" STUDENT_NAME "_" STUDENT_LASTNAME "_" STUDENT_ID ".txt";
  char* file = xrealloc(NULL, strlen(base) + strlen(suffix) + 1);
  strcpy(file, base);
  strcat(file, suffix);
  char* out_path = join_path(outdir, file);
  free(base);
  free(file);

  int ok = 0;
  FILE* f = fopen(out_path, "wb");
  if (f != NULL) {
    size_t written = out->len ? fwrite(out->data, 1, out->len, f) : 0;
    ok = (fclose(f) == 0) && written == out->len;
  }
  if (!ok) fprintf(stderr, "calc: cannot write output file '%s'\n", out_path);
  free(out_path);
  return ok;
}

// Default output folder <input_base>_<name>_<studentid>.
static char* default_outdir(const char* input, int is_file) {
  char* base = base_name(input, is_file);
  const char* suffix = "_" STUDENT_NAME "_" STUDENT_ID;
  char* r = xrealloc(NULL, strlen(base) + strlen(suffix) + 1);
  strcpy(r, base);
  strcat(r, suffix);
  free(base);
  return r;
}

static int cmp_str(const void* a, const void* b) {
  return strcmp(*(char* const*)a, *(char* const*)b);
}

// For -d. First I collect all .txt names and close the folder, and only then
// write the outputs. Otherwise if OUTDIR is the same folder the new files
// could be read as inputs too.
static int process_dir(const char* dir, const char* outdir) {
  DIR* d = opendir(dir);
  if (d == NULL) {
    fprintf(stderr, "calc: cannot open input folder '%s': %s\n", dir,
            strerror(errno));
    return 0;
  }
  char** paths = NULL;
  size_t count = 0, cap = 0;
  struct dirent* e;
  while ((e = readdir(d)) != NULL) {
    if (!has_txt_suffix(e->d_name)) continue;
    char* path = join_path(dir, e->d_name);
    struct stat st;
    // Only normal files, subfolders are skipped.
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
      free(path);
      continue;
    }
    if (count == cap) {
      cap = cap ? cap * 2 : 16;
      paths = xrealloc(paths, cap * sizeof *paths);
    }
    paths[count++] = path;
  }
  closedir(d);
  if (count > 0) qsort(paths, count, sizeof *paths, cmp_str);

  int ok = ensure_dir(outdir);
  Buf in = {0}, out = {0};
  for (size_t k = 0; k < count; k++) {
    // If one file fails I still do the others, but the exit code will be 1.
    if (ok && !process_file(paths[k], outdir, &in, &out)) ok = 0;
    free(paths[k]);
  }
  free(paths);
  free(in.data);
  free(out.data);
  return ok;
}

static void usage(void) {
  fprintf(stderr,
          "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] "
          "[input.txt]\n");
}

int main(int argc, char** argv) {
  const char *dir = NULL, *outdir = NULL, *input = NULL;

  if (argc < 2) {
    usage();
    return 1;
  }
  for (int k = 1; k < argc; k++) {
    const char* a = argv[k];
    int is_dir = strcmp(a, "-d") == 0 || strcmp(a, "--dir") == 0;
    int is_out = strcmp(a, "-o") == 0 || strcmp(a, "--output-dir") == 0;
    if (is_dir || is_out) {
      if (k + 1 >= argc) {
        fprintf(stderr, "calc: option '%s' needs a value\n", a);
        usage();
        return 1;
      }
      if (is_dir)
        dir = argv[++k];
      else
        outdir = argv[++k];
    } else if (a[0] == '-' && a[1] != '\0') {
      fprintf(stderr, "calc: unknown option '%s'\n", a);
      usage();
      return 1;
    } else if (input != NULL) {
      fprintf(stderr, "calc: more than one input file given\n");
      usage();
      return 1;
    } else {
      input = a;
    }
  }
  if ((dir == NULL) == (input == NULL)) {
    fprintf(stderr, "calc: give either an input file or -d DIR\n");
    usage();
    return 1;
  }

  char* def = NULL;
  if (outdir == NULL)
    outdir = def = default_outdir(dir ? dir : input, dir == NULL);

  int ok;
  if (dir != NULL) {
    ok = process_dir(dir, outdir);
  } else {
    // I check the input first so no empty output folder is made if the file is
    // missing.
    FILE* f = fopen(input, "rb");
    if (f == NULL) {
      fprintf(stderr, "calc: cannot open input file '%s': %s\n", input,
              strerror(errno));
      ok = 0;
    } else {
      fclose(f);
      Buf in = {0}, out = {0};
      ok = ensure_dir(outdir) && process_file(input, outdir, &in, &out);
      free(in.data);
      free(out.data);
    }
  }
  free(def);
  return ok ? 0 : 1;
}
