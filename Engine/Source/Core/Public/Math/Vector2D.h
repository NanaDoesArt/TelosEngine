#pragma once

#include <rtm/vector4f.h>
#include <rtm/vector4d.h>
#include <cmath>
#include <type_traits>

namespace Math
{
    namespace Detail
    {
        // Traits helper to dispatch float vs double to the right RTM type
        template<typename T> struct RTMVectorTraits;

        template<>
        struct RTMVectorTraits<float>
        {
            using SimdType = rtm::vector4f;
            static SimdType Load(float x, float y) { return rtm::vector_set(x, y, 0.0f, 0.0f); }
            static float GetX(SimdType v) { return rtm::vector_get_x(v); }
            static float GetY(SimdType v) { return rtm::vector_get_y(v); }
        };

        template<>
        struct RTMVectorTraits<double>
        {
            using SimdType = rtm::vector4d;
            static SimdType Load(double x, double y) { return rtm::vector_set(x, y, 0.0, 0.0); }
            static double GetX(SimdType v) { return rtm::vector_get_x(v); }
            static double GetY(SimdType v) { return rtm::vector_get_y(v); }
        };
    }

    template<typename T>
    struct TVector2
    {
        static_assert(std::is_floating_point_v<T>, "TVector2 only supports float or double.");
        using Traits = Detail::RTMVectorTraits<T>;
        using SimdType = typename Traits::SimdType;

        T X;
        T Y;

        // Constructors (UE5 style)
        TVector2() = default;
        constexpr TVector2(T InX, T InY) : X(InX), Y(InY) {}
        explicit constexpr TVector2(T InF) : X(InF), Y(InF) {}

        // Conversion between float and double TVector2
        template<typename FArg>
        explicit constexpr TVector2(const TVector2<FArg>& From)
            : X(static_cast<T>(From.X)), Y(static_cast<T>(From.Y)) {}

        // Static constants
        static const TVector2<T> ZeroVector;
        static const TVector2<T> UnitVector;

        // RTM Conversion Helpers
        SimdType ToRTM() const { return Traits::Load(X, Y); }
        static TVector2<T> FromRTM(SimdType v) { return TVector2<T>(Traits::GetX(v), Traits::GetY(v)); }

        // Operators
        constexpr TVector2<T> operator+(const TVector2<T>& V) const { return TVector2<T>(X + V.X, Y + V.Y); }
        constexpr TVector2<T> operator-(const TVector2<T>& V) const { return TVector2<T>(X - V.X, Y - V.Y); }
        constexpr TVector2<T> operator*(T Scale) const { return TVector2<T>(X * Scale, Y * Scale); }
        constexpr TVector2<T> operator/(T Scale) const { const T RScale = T(1) / Scale; return TVector2<T>(X * RScale, Y * RScale); }
        constexpr TVector2<T> operator-() const { return TVector2<T>(-X, -Y); }

        TVector2<T>& operator+=(const TVector2<T>& V) { X += V.X; Y += V.Y; return *this; }
        TVector2<T>& operator-=(const TVector2<T>& V) { X -= V.X; Y -= V.Y; return *this; }
        TVector2<T>& operator*=(T Scale) { X *= Scale; Y *= Scale; return *this; }

        // RTM-Backed Math Operations
        T Length() const
        {
            return rtm::vector_length(ToRTM());
        }

        T LengthSquared() const
        {
            return rtm::vector_length_squared(ToRTM());
        }

        TVector2<T> GetSafeNormal(T Tolerance = T(1e-8)) const
        {
            const T SquareSum = X * X + Y * Y;
            if (SquareSum > Tolerance)
            {
                // Uses RTM SIMD normalization
                return FromRTM(rtm::vector_normalize(ToRTM()));
            }
            return ZeroVector;
        }

        static T DotProduct(const TVector2<T>& A, const TVector2<T>& B)
        {
            return rtm::vector_dot(A.ToRTM(), B.ToRTM());
        }

        static T Distance(const TVector2<T>& A, const TVector2<T>& B)
        {
            return rtm::vector_distance(A.ToRTM(), B.ToRTM());
        }
    };

    // Out-of-class static constant definitions
    template<typename T> inline constexpr TVector2<T> TVector2<T>::ZeroVector(T(0), T(0));
    template<typename T> inline constexpr TVector2<T> TVector2<T>::UnitVector(T(1), T(1));
}

// Global Engine Type Aliases (UE5 style)
using FVector2f = Math::TVector2<float>;
using FVector2d = Math::TVector2<double>;
using FVector2D = Math::TVector2<float>;