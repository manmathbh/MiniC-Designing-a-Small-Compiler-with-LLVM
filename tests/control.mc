// control.mc — tests if/else and while.
// Expected output:
//   1
//   3
//   5
int main() {
    // if/else: x starts at 0, branch sets it to 1
    int x = 0;
    if (x == 0) {
        x = 1;
    } else {
        x = 2;
    }
    print(x); // 1

    // while: count up to 3
    int i = 0;
    while (i < 3) {
        i = i + 1;
    }
    print(i); // 3

    // Nested: sum 1+2+...+n where n=3 → 6? No: 1+2=3, 3+... wait
    // Let's do: accumulate 1+2+...  while i < 4 starting from i=1
    int sum = 0;
    int j = 1;
    while (j <= 4) {
        sum = sum + j;
        j = j + 1;
    }
    // sum = 1+2+3+4 = 10, but let's use a simpler check: print sum/2 = 5
    print(sum / 2); // 5

    return 0;
}
