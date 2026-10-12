// fibonacci.mc — tests function declarations and recursion.
// Expected output:
//   55
//   8
int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int double_it(int x) {
    return x * 2;
}

int main() {
    print(fib(10));       // 55
    print(double_it(4));  // 8
    return 0;
}
