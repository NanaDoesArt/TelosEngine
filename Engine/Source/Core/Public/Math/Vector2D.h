#pragma once

#include <rtm/vector4d.h>
#include <rtm/vector4f.h>

#include <type_traits>

// ==============================================================================
// 2D vector, with the arithmetic done by Realtime Math (RTM).
//
// Storage is two scalars; SIMD happens inside each operation, not in the members.
// Do not store rtm::vector4f in an object instead: it is a 16-byte register type that
// forces 16-byte alignment and quadruples the bytes EnTT copies when it moves an entity
// between archetypes. Convert at the operation boundary, as done below.
//
// RTM is header-only and namespace-level (rtm::), so nothing here is exported from Core:
// every module compiles the same inline functions.
// ==============================================================================

namespace Math
{
    namespace Detail
    {
        // Dispatches float/double to the matching RTM type.
        template <typename T> struct RTMVectorTraits;

        template <>
        struct RTMVectorTraits<float>
        {
            using SimdType = rtm::vector4f;
            static SimdType Load(float X, float Y) { return rtm::vector_set(X, Y, 0.0f, 0.0f); }
            static float GetX(SimdType V) { return rtm::vector_get_x(V); }
            static float GetY(SimdType V) { return rtm::vector_get_y(V); }
        };

        template <>
        struct RTMVectorTraits<double>
        {
            using SimdType = rtm::vector4d;
            static SimdType Load(double X, double Y) { return rtm::vector_set(X, Y, 0.0, 0.0); }
            static double GetX(SimdType V) { return rtm::vector_get_x(V); }
            static double GetY(SimdType V) { return rtm::vector_get_y(V); }
        };
    }

    template <typename T>
    struct TVector2
    {
        static_assert(std::is_floating_point_v<T>, "TVector2 only supports float or double.");

        using Traits = Detail::RTMVectorTraits<T>;
        using SimdType = typename Traits::SimdType;

        // Zero-initialised on purpose: a default-constructed math type holding garbage is
        // the usual source of NaN positions, and this costs nothing.
        T X = T(0);
        T Y = T(0);

        TVector2() = default;
        constexpr TVector2(T InX, T InY) : X(InX), Y(InY) {}
        explicit constexpr TVector2(T InValue) : X(InValue), Y(InValue) {}

        // float <-> double. Explicit: narrowing a double vector to float must be visible.
        template <typename FArg>
        explicit constexpr TVector2(const TVector2<FArg>& From)
            : X(static_cast<T>(From.X)), Y(static_cast<T>(From.Y)) {}

        // Defined after the class: a static member cannot be initialised inside its own
        // incomplete type. inline keeps the header ODR-safe across modules.
        static const TVector2<T> ZeroVector;
        static const TVector2<T> UnitVector;

        SimdType ToRTM() const { return Traits::Load(X, Y); }
        static TVector2<T> FromRTM(SimdType V) { return TVector2<T>(Traits::GetX(V), Traits::GetY(V)); }

        constexpr TVector2<T> operator+(const TVector2<T>& V) const { return TVector2<T>(X + V.X, Y + V.Y); }
        constexpr TVector2<T> operator-(const TVector2<T>& V) const { return TVector2<T>(X - V.X, Y - V.Y); }
        constexpr TVector2<T> operator*(T Scale) const { return TVector2<T>(X * Scale, Y * Scale); }
        constexpr TVector2<T> operator/(T Scale) const { const T RScale = T(1) / Scale; return TVector2<T>(X * RScale, Y * RScale); }
        constexpr TVector2<T> operator-() const { return TVector2<T>(-X, -Y); }

        TVector2<T>& operator+=(const TVector2<T>& V) { X += V.X; Y += V.Y; return *this; }
        TVector2<T>& operator-=(const TVector2<T>& V) { X -= V.X; Y -= V.Y; return *this; }
        TVector2<T>& operator*=(T Scale) { X *= Scale; Y *= Scale; return *this; }

        T Length() const { return rtm::vector_length(ToRTM()); }
        T LengthSquared() const { return rtm::vector_length_squared(ToRTM()); }

        // Tolerance is a length. Comparing a squared magnitude against a length would make the
        // threshold scale with the square of the vector size, which is silently wrong for
        // anything but unit-scale coordinates.
        TVector2<T> GetSafeNormal(T Tolerance = T(1e-4)) const
        {
            const T SquareSum = X * X + Y * Y;
            if (SquareSum > Tolerance * Tolerance)
            {
                // *_2 variants operate on x/y and leave z/w at zero, which is what a 2D vector means.
                return FromRTM(rtm::vector_normalize2(ToRTM()));
            }
            return ZeroVector;
        }

        static T DotProduct(const TVector2<T>& A, const TVector2<T>& B)
        {
            return rtm::vector_dot(A.ToRTM(), B.ToRTM());
        }

        static T Distance(const TVector2<T>& A, const TVector2<T>& B)
        {
            // RTM has no 2D distance: vector_distance3 is the 3D length, and z is zero here.
            return rtm::vector_distance3(A.ToRTM(), B.ToRTM());
        }
    };

    template <typename T> inline const TVector2<T> TVector2<T>::ZeroVector(T(0), T(0));
    template <typename T> inline const TVector2<T> TVector2<T>::UnitVector(T(1), T(1));
}
