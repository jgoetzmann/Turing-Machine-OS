/* strcat.c -- concatenates two char arrays into a third by copying bytes,
 * then prints the result with puts(). The three arrays sit in the image's
 * data area right after the code, so the strip panel shows the copy.
 * Expected output: "helloworld\n" */

char a[8] = "hello";
char b[8] = "world";
char out[16];

int main() {
    int i = 0;
    int j = 0;
    while (a[i] != 0) {
        out[j] = a[i];
        i++;
        j++;
    }
    i = 0;
    while (b[i] != 0) {
        out[j] = b[i];
        i++;
        j++;
    }
    out[j] = 0;
    puts(out);
    return 0;
}
