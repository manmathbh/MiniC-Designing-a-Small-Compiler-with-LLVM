// arith.mc — tests arithmetic precedence and parenthesisation.
// Expected output:
//   20
//   30
//   7
//   1
int main() {
    // Operator precedence: * before +
    int x = 10 + 5 * 2;   // 20
    print(x);

    // Parentheses override precedence
    int y = (10 + 5) * 2; // 30
    print(y);

    // Mixed: 2 + 10 / 2 + 1 = 2 + 5 + 1 = 8? No: 1+2*3 = 7
    int z = 1 + 2 * 3;    // 7
    print(z);

    // Modulo
    int m = 10 % 3;        // 1
    print(m);

    return 0;
}
