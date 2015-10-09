#include <stdio.h>

int main(int argc, char** argv) {
  int number = 0;
  int another_number = 1;
  int yet_another = 2;
  number = another_number + yet_another;
  printf("Hello debugger: %d\n", number);
  return(0);
}
