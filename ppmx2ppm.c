#include <stdio.h>
#include <stdlib.h>

void usage(char** argv) {
    fprintf(stderr, "Usage: %s infile outfile\n", argv[0]);
    exit(1);
}

int main(int argc, char** argv) {
    if (argc != 3)
        usage(argv);
    FILE* in = fopen(argv[1], "rb");
    if (!in) {
        perror("Input file");
        usage(argv);
    }
    FILE* out = fopen(argv[2], "wb");
    if (!out) {
        perror("Output file");
        usage(argv);
    }
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (c != '!') {
            fputc(c, out);
        } else {
            int r, g, b;
            if (fscanf(in, "%x!%x!%x", &r, &g, &b) != 3) {
                fprintf(stderr, "Can't parse input\n");
                exit(1);
            }
            fputc(r, out);
            fputc(g, out);
            fputc(b, out);
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}
