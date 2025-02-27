# CPP Utils
This directory contains a wealth of C++ classes that have been found useful when working in C++ in conjunction
with the ESP-IDF.  The classes have been documented using `doxygen` so one can run a doxygen processor over them
to create the user guides and programming references.

The work was originally published by Kolban, but the repository is now unmaintained at https://github.com/nkolban/esp32-snippets and no longer works with recent IDF updates

# Notes
The work here was adapted to compile and work with ESP IDF 5.4. I also added some support for ESP32S3, however, the S3 doesn't support PWM at high frequencies, so the PWM module has been disabled. I am not familiar enough with I2S and unable to adapt I2S to the IDF 5.4 or the ESP32S3, so it has also been disabled. WiFi, especially, the module that made me take on this work, has been tested and works well, allowing to switch between AP and STA modes without crashing. Mostly, the rest of the work is as it was, only adapted to renamed or changed structures in the IDF. Help with keeping this current is appreciated.

# Compiling the C++ classes
The C++ classes found here exist as an ESP-IDF component.  To build the classes and then use them in your project perform the following
steps:

1. Create an ESP-IDF project.
2. Create a directory called `components` in the root of your ESP-IDF project.
3. Copy this directory (`cpp_utils`) into your new `components` directory.  The result will be `<project>/components/cpp_utils/<files>`.
4. In your ESP-IDF project build as normal.

The C++ classes will be compiled and available to be used in your own code.

# Adding a main function
When working with C++, your calling function should also be written in C++.  Consider replacing your `main.c` with the following
`main.cpp` file:

```
extern "C" {
   void app_main();
}

void app_main() {
   // Your code goes here
}
```

The way to read the above is that we are defining a global function called `app_main` but we are saying that its external
linkage (i.e. how it is found and called) is using the C language convention.  However, since the source file is `main.cpp` and
hence compiled by the C++ compiler, you can utilize C++ classes and language features within and, since it has C linkage, it will
satisfy the ESP-IDF environment as the entry point into your own code.

## BLE Functions
The Bluetooth BLE functions are only compiled if Bluetooth is enabled in `make menuconfig`.  This is primarily because
the ESP-IDF build system has chosen to only compile the underlying BLE functions if Bluetooth is enabled.

## Building the Documentation
The code is commented using the Doxygen tags.  As such we can run Doxygen to generate the data.  I use `doxywizard` using
the `Doxyfile` located in this directory.

## Building the Arduino libraries
Some of the classes in this package also have applicability in an Arduino environment.  A `Makefile` called `Makefile.arduino` is provided to build the libraries.  For example:

```
$ make -f Makefile.arduino
```

The results of this will be ZIP files found in the `Arduino` directory relative to this one.  Targets include:

* `build_ble` - Build the BLE libraries. See also: [Arduino BLE Support](ArduinoBLE.md) .
