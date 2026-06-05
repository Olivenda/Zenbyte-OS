/* FILEVIEW.ZBX -- Simple file viewer */
int main() {
    int fd, ch, row, col;
    cls(0x07);
    window(1, 5, 70, 22, "File Viewer");
    at_puts(3, 8, 0x0E, "Enter filename: ");
    present();
    /* Simple filename input (max 30 chars) */
    char name[32];
    int len = 0;
    for (;;) {
        ch = waitkey();
        if (ch == KEY_ENTER || ch == KEY_ESC) break;
        if (ch == KEY_BACK && len > 0) {
            len--;
            name[len] = 0;
            putcell(3, 24 + len, ' ', 0x07);
        } else if (ch >= 32 && ch < 127 && len < 30) {
            name[len] = ch;
            len++;
            name[len] = 0;
            putcell(3, 24 + len - 1, ch, 0x0F);
        }
        present();
    }
    if (ch == KEY_ESC || len == 0) return 0;
    /* Try to open and display the file */
    fd = fopen(name);
    if (fd < 0) {
        at_puts(5, 8, 0x0C, "Error: Cannot open file");
        present();
        waitkey();
        return 1;
    }
    cls(0x07);
    window(1, 1, 78, 23, name);
    row = 3; col = 3;
    for (;;) {
        ch = fgetc(fd);
        if (ch < 0) break;
        if (ch == '\n' || ch == '\r') {
            row++;
            col = 3;
            if (row > 21) break;
        } else if (ch == '\t') {
            col = col + 4;
        } else {
            putcell(row, col, ch, 0x07);
            col++;
            if (col > 75) { row++; col = 3; }
        }
    }
    fclose(fd);
    at_puts(22, 3, 0x0A, "Press any key to exit");
    present();
    waitkey();
    return 0;
}
