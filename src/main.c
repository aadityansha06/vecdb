

#include "../include/terminal.h"

//void print_usage();


int main(int argc, char *argv[]) {
   /* if (argc < 2) return print_usage();

 //   if (strcmp(argv[1], "server") == 0) {
        return start_tcp_server(argc, argv); // Route to TCP module
    } else {
       // return run_tui(argc, argv); // Route to Terminal module
    }
*/

    run_tui(argc, argv);
 return 0; 
}
