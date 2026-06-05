/* GUESS2.ZBX -- Number guessing game */
int main() {
    int secret, guess, tries;
    cls(0x1B);
    window(4, 15, 50, 14, "Number Guessing Game");
    at_puts(6, 18, 0x0F, "I'm thinking of a number 1-100.");
    at_puts(7, 18, 0x0F, "You have 7 tries to guess it!");
    at_puts(9, 18, 0x0E, "Press any key to start...");
    present();
    waitkey();
    secret = rand_range(1, 100);
    tries = 0;
    for (;;) {
        cls(0x1B);
        window(4, 15, 50, 14, "Guess the Number");
        char msg[40];
        int n;
        n = ksnprintf(msg, 40, "Try %d of 7", tries + 1);
        at_puts(6, 18, 0x0E, msg);
        at_puts(8, 18, 0x0F, "Enter your guess (1-100):");
        present();
        /* Read guess */
        int g = 0;
        for (;;) {
            int k = waitkey();
            if (k == KEY_ENTER && g > 0) break;
            if (k == KEY_ESC) return 0;
            if (k == KEY_BACK && g > 0) {
                g = g / 10;
                putcell(8, 44, ' ', 0x0F);
                putcell(8, 43, ' ', 0x0F);
            } else if (k >= '0' && k <= '9') {
                g = g * 10 + (k - '0');
                if (g > 100) g = 100;
                n = ksnprintf(msg, 40, "%d", g);
                at_puts(8, 42, 0x0F, msg);
            }
            present();
        }
        guess = g;
        tries++;
        if (guess == secret) {
            cls(0x2B);
            window(8, 20, 40, 8, "You Win!");
            n = ksnprintf(msg, 40, "Got it in %d tries!", tries);
            at_puts(11, 24, 0x0F, msg);
            at_puts(13, 24, 0x0A, "Press any key to exit");
            present();
            waitkey();
            return 0;
        }
        cls(0x1B);
        window(6, 20, 40, 8, "Hint");
        if (guess < secret) {
            at_puts(9, 24, 0x0E, "Too LOW! Try higher.");
        } else {
            at_puts(9, 24, 0x0E, "Too HIGH! Try lower.");
        }
        n = ksnprintf(msg, 40, "Your guess: %d  Secret: %s", guess,
                      guess < secret ? "higher" : "lower");
        at_puts(11, 24, 0x0F, msg);
        present();
        delay(50);
    }
}
