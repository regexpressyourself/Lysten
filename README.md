<img alt="Lysten Logo" align="right" src="https://regexpressyourself.github.io/public/lysten-logo.png" width="400px" />

# Lysten

A general-purpose server, implemented using multiplexed I/O with epoll and custom thread pools for concurrency on a multicore system.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Deployment](#deployment)
2. [Built With](#built-with)
3. [Contributing](#contributing)
3. [Authors](#authors)
3. [License](#license)


## Getting Started

Lysten uses either static or shared libraries in order to incorporate the thread pool. These libraries can be set with `make`. 

### Setting up Libraries 

#### Static Library Compilation

```
   make package-static-lib
```

#### Shared Library Compilation

```
   make package-shared-lib
```

### Compiling

To compile Lysten using the shared thread pool libraries, run:

```
   make compile-shared
```

To compile Lysten using the static thread pool libraries, run:

```
   make compile-static
```

### Debugging

To compile Lysten with debugging statements on, run:

```
   make compile-debug
```

_Note: This compiles using the thread pool's static libraries, so make sure to run `make package-static-lib` first._

### Testing

Included is a test script to pelt the server with multiple clients all at once. You can run this test with: 

```
  make test-server
```

By default, the test script will spin up 10 clients who each run 10 cycles or commands. This can be changed by running `./test/testserver.sh` directly

### Running

To run the server, execute:

``` 
    make run-server
```

To start a client, execute:

``` 
    make run-client
```

**[Back to top](#table-of-contents)**

## Built With

* [C](https://en.wikipedia.org/wiki/C_(programming_language)) - The C language itself
* [Epoll](http://man7.org/linux/man-pages/man7/epoll.7.html) - Multiple epoll units are used to efficiently retrieve tasks from a job queue
* [Thread Pool](https://en.wikipedia.org/wiki/Thread_pool) - A custom thread pool is used to disperse tasks across cores
* [Pseudo TTYs](https://en.wikipedia.org/wiki/Pseudoterminal) - Psuedo terminals spin up to run Bash ommands remotely

**[Back to top](#table-of-contents)**

## Contributing

As always, I'm very happy to receive pull requests, questions/issues regarding code, and feature requests. 

**[Back to top](#table-of-contents)**

## Authors

* **[Sam Messina](https://www.github.com/regexpressyourself)** - *Sole Developer* 

**[Back to top](#table-of-contents)**

## License

Lysten is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details.

**[Back to top](#table-of-contents)**

