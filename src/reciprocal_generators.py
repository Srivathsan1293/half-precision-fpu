# generate_rom.py

with open("reciprocal_rom.mem", "w") as f:
    for addr in range(1024):
        # The 10-bit address represents the fractional bits.
        # Add 1.0 for the implicit hidden bit.
        mantissa = 1.0 + (addr / 1024.0)

        # Calculate the mathematical reciprocal
        reciprocal = 1.0 / mantissa

        # Shift radix point by 13 bits for hardware alignment (1 int bit, 13 frac bits)
        # Round to nearest integer to satisfy Guard/Round/Sticky bit accuracy
        val_int = round(reciprocal * 8192)

        # Format as a 14-bit binary string and write to file
        bin_str = f"{val_int:014b}"
        f.write(f"{bin_str}\n")

print("Generated reciprocal_rom.mem with 1024 entries.")
