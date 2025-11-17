// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include <windows.h>
#include <cmath>
#include <cstdint>

#define ILINE inline
#define STRUCT_INFO
#define AUTO_STRUCT_INFO STRUCT_INFO
#define SAFE_RELEASE(p)       { if (p) { (p)->Release();      (p) = NULL; } }
#define cry_strcpy(dst, src) strcpy_s(dst, _countof(dst), src)

typedef unsigned int EntityId;
typedef float f32;
typedef double f64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef double real;


enum type_zero { ZERO };

enum type_min { VMIN };

enum type_max { VMAX };

enum type_identity { IDENTITY };

//////////////////////////////////////////////////////////////////////////
// Interlocked API
//////////////////////////////////////////////////////////////////////////


// Returns the resulting incremented value
inline LONG CryInterlockedIncrement(volatile int* pDst)
{
    static_assert(sizeof(int) == sizeof(LONG), "Unsecured cast. int is not same size as LONG.");
    return _InterlockedIncrement(reinterpret_cast<volatile LONG*>(pDst));
}

// Returns the resulting decremented value
inline LONG CryInterlockedDecrement(volatile int* pDst)
{
    static_assert(sizeof(int) == sizeof(LONG), "Unsecured cast. int is not same size as LONG.");
    return _InterlockedDecrement(reinterpret_cast<volatile LONG*>(pDst));
}


struct Vec3
{
    float x, y, z;

    Vec3() : x(0.0f), y(0.0f), z(0.0f)
    {
    }

    Vec3(float x, float y, float z) : x(x), y(y), z(z)
    {
    }

    Vec3 operator*(float scalar) const
    {
        return {x * scalar, y * scalar, z * scalar};
    }

    Vec3 operator+(const Vec3& other) const
    {
        return {x + other.x, y + other.y, z + other.z};
    }

    Vec3 operator-(const Vec3& other) const
    {
        return {x - other.x, y - other.y, z - other.z};
    }

    Vec3& operator+=(const Vec3& other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vec3& operator-=(const Vec3& other)
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vec3 Cross(const Vec3& other) const
    {
        return {y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x};
    }

    float Length() const
    {
        return sqrtf(x * x + y * y + z * z);
    }

    Vec3 Normalized() const
    {
        float len = Length();
        return (len > 0) ? (*this * (1.0f / len)) : Vec3{0, 0, 0};
    }
};

struct Ang3 : public Vec3
{
    Ang3() : Vec3()
    {
    }

    Ang3(float x, float y, float z) : Vec3(x, y, z)
    {
    }
};
