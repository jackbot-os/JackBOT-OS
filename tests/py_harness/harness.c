/* Per-case Python subset regression. Prints PASS/FAIL and actual output per case. */
#include <stdio.h>
#include <string.h>
#include <kernel/py.h>

static int n_pass, n_fail;

static void case_eq(const char *src, const char *expect)
{
    char out[400];
    py_init();
    int rc = py_exec(src, out, sizeof(out));
    int ok = (rc == 0) && strcmp(out, expect) == 0;
    if (ok) n_pass++; else n_fail++;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", src);
    if (!ok) {
        printf("      expect: [%s]\n", expect);
        printf("      actual: [%s] (rc=%d)\n", out, rc);
    }
}

static void case_err(const char *src)
{
    char out[400];
    py_init();
    int rc = py_exec(src, out, sizeof(out));
    int ok = rc < 0;
    if (ok) n_pass++; else n_fail++;
    printf("[%s] (should error) %s\n", ok ? "PASS" : "FAIL", src);
    if (!ok) printf("      actual: [%s] rc=%d\n", out, rc);
}

int main(void)
{
    case_eq("1+2*3", "7\n");
    case_eq("(1+2)*3", "9\n");
    case_eq("7//2", "3\n");
    case_eq("7%3", "1\n");
    case_eq("2**10", "1024\n");
    case_eq("0xff", "255\n");
    case_eq("0b1010", "10\n");
    case_eq("1<2 and 3>2", "1\n");
    case_eq("10 & 6", "2\n");
    case_eq("1 << 4", "16\n");
    case_eq("12 ^ 10", "6\n");
    case_eq("abs(-4)", "4\n");
    case_eq("sum([1,2,3,4])", "10\n");
    case_eq("max([3,9,2])", "9\n");
    case_eq("sorted([3,1,2])", "[1, 2, 3]\n");
    case_eq("range(2,6)", "[2, 3, 4, 5]\n");
    case_eq("range(0,10,3)", "[0, 3, 6, 9]\n");
    case_eq("len('hello')", "5\n");
    case_eq("'ab' + 'cd'", "abcd\n");
    case_eq("'ab' * 3", "ababab\n");
    case_eq("'abc'.upper()", "ABC\n");
    case_eq("'  Hello  '.strip()", "Hello\n");
    case_eq("'a,b,c'.split()", "['a,b,c']\n");
    case_eq("s='hi'\ns", "hi\n");
    case_eq("s='hi'\nt=s+'!'\nt", "hi!\n");
    case_eq("a=[1,2,3]\na.append(4)\na", "[1, 2, 3, 4]\n");
    case_eq("a=[1,2,3]\na[0]=9\na", "[9, 2, 3]\n");
    case_eq("d={'a':1}\nd['b']=2\nd['b']", "2\n");
    case_eq("a=0\nfor i in range(5):\n    a=a+i\na", "10\n");
    case_eq("n=5\nr=1\nwhile n>1:\n    r=r*n\n    n=n-1\nr", "120\n");
    case_eq("x=7\nif x>5:\n    y=100\nelse:\n    y=1\ny", "100\n");
    case_eq("x=2\nif x>5:\n    y=1\nelif x>1:\n    y=2\nelse:\n    y=3\ny", "2\n");
    case_eq("def add(a,b):\n    return a+b\nadd(3,4)", "7\n");
    case_eq("def fact(n):\n    if n<=1:\n        return 1\n    return n*fact(n-1)\nfact(5)", "120\n");
    case_eq("def fib(n):\n    if n<2:\n        return n\n    return fib(n-1)+fib(n-2)\nfib(10)", "55\n");
    case_eq("tot=0\nfor i in range(10):\n    if i%2==0:\n        tot=tot+i\n    else:\n        tot=tot+0\ntot", "20\n");
    case_eq("a=[]\nfor i in range(4):\n    a.append(i*i)\na[3]", "9\n");
    case_eq("for i in range(3):\n    if i==1:\n        break\n    z=i\nz", "0\n");
    case_eq("s=0\nfor i in range(5):\n    if i==2:\n        continue\n    s=s+i\ns", "8\n");
    case_eq("n=0\nfor i in range(3):\n    for j in range(3):\n        n=n+1\nn", "9\n");
    case_eq("chr(65)", "A\n");
    case_eq("ord('A')", "65\n");
    case_eq("list('ab')", "['a', 'b']\n");

    case_err("1/0");
    case_err("undefined_xyz");
    case_err("a=[1]\na[5]=9");

    case_eq("len([])", "0\n");
    case_eq("len('abc')", "3\n");
    case_eq("d={'x':1,'y':2}\nd['z']=3\nlen(d)", "3\n");
    case_eq("'a,b'.replace('a','X')", "X,b\n");
    case_eq("len(range(120))", "120\n");

    {
        char out[400];
        py_init();
        char longtok[300];
        for (int i = 0; i < 298; i++) longtok[i] = 'a';
        longtok[298] = 0;
        char src[400];
        snprintf(src, sizeof(src), "s='%s'\nw=s.split()\nlen(w)", longtok);
        int rc = py_exec(src, out, sizeof(out));
        int ok = (rc == 0);
        if (ok) n_pass++; else n_fail++;
        printf("[%s] long-token split (must not crash)\n", ok ? "PASS" : "FAIL");
        if (!ok) printf("      actual: [%s] rc=%d\n", out, rc);
    }

    printf("== per-case PASS=%d FAIL=%d ==\n", n_pass, n_fail);
    printf("== built-in py_self_test below ==\n");
    py_self_test();
    return n_fail ? 1 : 0;
}
