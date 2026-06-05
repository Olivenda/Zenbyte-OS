/* SYSINFO.ZBX -- Display system information */
int main() {
    cls(0x1B);
    window(2, 10, 60, 20, "System Information");
    at_puts(4, 14, 0x0F, "Zenbite System Information");
    at_puts(6, 14, 0x0E, "OS: Zenbite v3.1 (32-bit)");
    at_puts(7, 14, 0x0E, "CPU: x86 protected mode");
    at_puts(8, 14, 0x0E, "Memory: Check shell 'mem' command");
    at_puts(10, 14, 0x0B, "Features:");
    at_puts(11, 16, 0x07, "* FAT12/16/32 filesystem");
    at_puts(12, 16, 0x07, "* USB keyboard + mouse");
    at_puts(13, 16, 0x07, "* Graphical desktop");
    at_puts(14, 16, 0x07, "* Built-in C interpreter");
    at_puts(15, 16, 0x07, "* Network stack (TCP/IP)");
    at_puts(17, 14, 0x0A, "Press any key to exit");
    present();
    waitkey();
    return 0;
}
