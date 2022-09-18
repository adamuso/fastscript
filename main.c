#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>

#include "executor.h"

// Stack holds values with the size of native pointer, for 64 bit architectures: 8 bytes
// All native types should have a size that fits in a stack value
//
// Block: 
//   Starts with '{' and ends with '}', consists of one or more statements
// 
// Statement:
//   Ends with a semicolon ';'. Consists of an expression or variable declaration.   
//   After all statements stack should be cleared, but not after variable declarations
//
// Variable declaration:
//   Starts with: 'let', 'const', 'var' or type name.
//   Declaration types:
//   - let - declares a mutable variable which have static type that is acquired from the assignment
//   - const - declares a non mutable variable which have a static type that is acquired from the assignment
//   - var - declares a mutable variable which have dynamic type
//   - <type name> - declares a mutable variable which have explictly defined type
//

int main() 
{
    // exec("\
    //     let X = struct { \
    //         i32 test; \
    //         \
    //         i32 new() => { \
    //             17 \
    //         } \
    //     }; \
    //     \
    //     var a = 2; \
    //     print(a); \
    //     a = add(a, 10); \
    //     print(a); \
    //     \
    //     var b = void(i32 x) => { \
    //         x = add(x, 5); \
    //         print(x); \
    //     }; \
    //     \
    //     b(a); \
    //     let c = X.new(); \
    //     print(c); \
    // ");

     exec("\
        var a = 2; \
        print(a); \
        a = add(a, 10); \
        print(a); \
        \
        var b = void(i32 x) => { \
            x = add(x, 5); \
            print(x); \
        }; \
        \
        b(a); \
    ");

    // exec("\
        struct X { \
            i32 test; \
            u8 abc; \
        } \
        X v = X { test: 10, abc: 3 }; \
        print(add(v.test, v.abc)); \
        let zero_test = [](X x) => { x.test = 0; }; \
        v.zero_test() \
        \
        var a = 2; \
        print(a); \
        a = add(a, 10); \
        print(a); \
        var b = [](var x) => { \
            x = add(x, 5); \
            print(x); \
        }; \
        b(a); \
        i32 c = b; \
    ");
    return 0;
}
