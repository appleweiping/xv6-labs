#include "kernel/types.h"
#include "user/user.h"

// Concurrent prime sieve (Doug McIlroy's pipeline).
// Each stage reads numbers from its left pipe, prints the first one (a
// prime), then creates a new right-hand stage and forwards every number
// that is not a multiple of that prime.

// Reads all numbers from the left pipe (fd), sieving out multiples of the
// first number read. Recurses via fork() to build the pipeline. Does not
// return (calls exit()).
__attribute__((noreturn))
static void
sieve(int left)
{
  int prime;

  // The first number arriving from the left is guaranteed prime.
  if(read(left, &prime, sizeof(prime)) != sizeof(prime)){
    // Nothing left to process.
    close(left);
    exit(0);
  }
  printf("prime %d\n", prime);

  int p[2];
  if(pipe(p) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // Child becomes the next stage: reads from p[0].
    close(p[1]);
    close(left);
    sieve(p[0]);
  } else {
    // Parent forwards non-multiples of prime to the child.
    close(p[0]);
    int n;
    while(read(left, &n, sizeof(n)) == sizeof(n)){
      if(n % prime != 0){
        if(write(p[1], &n, sizeof(n)) != sizeof(n)){
          fprintf(2, "primes: write failed\n");
          exit(1);
        }
      }
    }
    close(left);
    close(p[1]); // signal EOF to the child
    wait(0);
    exit(0);
  }
}

int
main(int argc, char *argv[])
{
  int p[2];
  if(pipe(p) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(p[1]);
    sieve(p[0]);
  } else {
    close(p[0]);
    for(int i = 2; i <= 35; i++){
      if(write(p[1], &i, sizeof(i)) != sizeof(i)){
        fprintf(2, "primes: write failed\n");
        exit(1);
      }
    }
    close(p[1]);
    wait(0);
    exit(0);
  }
}
