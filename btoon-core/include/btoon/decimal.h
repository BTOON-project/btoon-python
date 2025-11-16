/**
 * @file decimal.h
 * @brief Arbitrary precision decimal type for financial data
 * 
 * Provides exact decimal arithmetic without floating-point rounding errors,
 * essential for financial calculations and currency operations.
 */

#ifndef BTOON_DECIMAL_H
#define BTOON_DECIMAL_H

#include <cstdint>
#include <string>
#include <vector>
#include <iosfwd>
#include <cmath>

namespace btoon {

/**
 * @brief Decimal number with arbitrary precision
 * 
 * Stores decimal numbers as a coefficient and exponent (coefficient × 10^exponent).
 * This representation avoids floating-point rounding errors.
 * 
 * Examples:
 *   123.45 = 12345 × 10^-2  (coefficient=12345, exponent=-2)
 *   0.001 = 1 × 10^-3        (coefficient=1, exponent=-3)
 *   1000 = 1 × 10^3          (coefficient=1, exponent=3)
 */
class Decimal {
public:
    // Constructors
    Decimal();
    explicit Decimal(int64_t value);
    explicit Decimal(double value);
    explicit Decimal(const std::string& str);
    Decimal(int64_t coefficient, int32_t exponent);
    
    // Factory methods
    static Decimal from_string(const std::string& str);
    static Decimal from_double(double value, int32_t precision = 15);
    static Decimal from_cents(int64_t cents); // For currency (e.g., $1.23 = 123 cents)
    
    // Accessors
    int64_t coefficient() const { return coefficient_; }
    int32_t exponent() const { return exponent_; }
    int32_t scale() const { return -exponent_; } // Number of decimal places
    
    // Conversion
    std::string to_string() const;
    double to_double() const;
    int64_t to_cents() const; // Convert to cents (assumes 2 decimal places)
    
    // Normalization
    Decimal normalize() const; // Remove trailing zeros
    Decimal round(int32_t decimal_places) const;
    Decimal truncate(int32_t decimal_places) const;
    
    // Arithmetic operations
    Decimal operator+(const Decimal& other) const;
    Decimal operator-(const Decimal& other) const;
    Decimal operator*(const Decimal& other) const;
    Decimal operator/(const Decimal& other) const;
    Decimal operator%(const Decimal& other) const;
    
    Decimal& operator+=(const Decimal& other);
    Decimal& operator-=(const Decimal& other);
    Decimal& operator*=(const Decimal& other);
    Decimal& operator/=(const Decimal& other);
    
    Decimal operator-() const; // Unary minus
    Decimal abs() const;
    
    // Comparison operators
    bool operator==(const Decimal& other) const;
    bool operator!=(const Decimal& other) const;
    bool operator<(const Decimal& other) const;
    bool operator<=(const Decimal& other) const;
    bool operator>(const Decimal& other) const;
    bool operator>=(const Decimal& other) const;
    
    // Special values
    bool is_zero() const { return coefficient_ == 0; }
    bool is_negative() const { return coefficient_ < 0; }
    bool is_positive() const { return coefficient_ > 0; }
    bool is_integer() const { return exponent_ >= 0; }
    
    // Financial operations
    Decimal multiply_and_round(const Decimal& other, int32_t decimal_places) const;
    Decimal divide_and_round(const Decimal& other, int32_t decimal_places) const;
    
    // Serialization for BTOON
    std::vector<uint8_t> to_bytes() const;
    static Decimal from_bytes(const std::vector<uint8_t>& bytes);
    
private:
    int64_t coefficient_;  // The significant digits
    int32_t exponent_;     // Power of 10
    
    // Helper methods
    static Decimal align_exponents(const Decimal& a, const Decimal& b, 
                                  int64_t& coeff_a, int64_t& coeff_b);
    static int64_t ipow10(int32_t exp);
    void reduce(); // Simplify representation
};

// Stream operators
std::ostream& operator<<(std::ostream& os, const Decimal& dec);
std::istream& operator>>(std::istream& is, Decimal& dec);

/**
 * @brief Currency type for common financial operations
 * 
 * Wrapper around Decimal with fixed 2 or 4 decimal places
 */
class Currency {
public:
    enum Precision {
        CENTS = 2,      // Most currencies (USD, EUR, etc.)
        SUBUNITS = 4    // Some currencies need more precision (BTC, gold, etc.)
    };
    
    Currency(const Decimal& amount, Precision precision = CENTS);
    Currency(double amount, Precision precision = CENTS);
    Currency(int64_t cents); // Assumes CENTS precision
    
    Decimal amount() const { return amount_; }
    Precision precision() const { return precision_; }
    
    Currency operator+(const Currency& other) const;
    Currency operator-(const Currency& other) const;
    Currency operator*(const Decimal& multiplier) const;
    Currency operator/(const Decimal& divisor) const;
    
    bool operator==(const Currency& other) const;
    bool operator<(const Currency& other) const;
    
    std::string to_string(bool with_symbol = false, const std::string& symbol = "$") const;
    int64_t to_cents() const;
    
private:
    Decimal amount_;
    Precision precision_;
    
    void round_to_precision();
};

/**
 * @brief Percentage type for financial calculations
 */
class Percentage {
public:
    explicit Percentage(const Decimal& value); // Value as percentage (e.g., 5.5 for 5.5%)
    explicit Percentage(double value);
    
    Decimal as_decimal() const { return value_ / Decimal(100); }
    Decimal as_basis_points() const { return value_ * Decimal(100); }
    
    Decimal apply_to(const Decimal& base) const;
    Currency apply_to(const Currency& base) const;
    
private:
    Decimal value_; // Stored as percentage
};

} // namespace btoon

#endif // BTOON_DECIMAL_H
