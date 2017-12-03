<img alt="Practice Buddy" align="right" src="https://regexpressyourself.github.io/public/practicebuddy_logo.png" width="400px" />

# Lysten

A general-purpose server, implemented using multiplexed I/O with epoll and custom thread pools for concurrency on a multicore system.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Deployment](#deployment)
2. [Built With](#built-with)
3. [Contributing](#contributing)
3. [Authors](#authors)
3. [License](#license)


## Introduction

Lysten is an implementation of an SSH server, but can be easily extended to a general purpose server, a la Nginx.

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

Note: This compiles using the thread pool's static libraries, so make sure to run `make package-static-lib` first.

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

* [React](https://facebook.github.io/react/) - The web framework that powers the site
* [Webpack](https://webpack.github.io/) - A module builder automate development and production build processes
* [React Router v. 4](https://reacttraining.com/react-router/) - Sits on top of React to enable route-based views without a backend server
* [Babel](https://babeljs.io/) - Transpiler to convert raw React code into browser-ready HTML and Javascript
* [create-react-app](https://github.com/facebookincubator/create-react-app) - Boilerplate React configuration from Facebook

**[Back to top](#table-of-contents)**

## Contributing

As always, I'm very happy to receive pull requests, questions/issues regarding code, and feature requests. 

Practice Buddy is under active development, poised for a complete feature set by January 2018. If you are interested in contributing, check out the existing [projects](https://github.com/regexpressyourself/PracticeBuddy/projects) and [issues](https://github.com/regexpressyourself/PracticeBuddy/issues), and contact me if anything sounds interesting to you.

**[Back to top](#table-of-contents)**

## Authors

* **[Sam Messina](https://www.github.com/regexpressyourself)** - *Sole Developer* 

**[Back to top](#table-of-contents)**

## License

Practice Buddy is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details.


**[Back to top](#table-of-contents)**

