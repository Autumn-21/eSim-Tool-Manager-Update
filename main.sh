#!/bin/bash

# Define file paths
FILES=(
    "./check-packages-final.sh"
    "./update-kicad-final.sh"
    "./update-dependency-final.sh"
    "./nghdl/update-ghdl-with-dependency.sh"
    "./nghdl/update-verilator-final.sh"
    "./nghdl/update-ngspice-final.sh"
)

# Package titles
TITLES=(
    "Check Packages"
    "Update KiCad"
    "Update Dependencies"
    "Update GHDL"
    "Update Verilator"
    "Update NGSPICE"
)

# Display options to the user
echo "Select the files you want to update (separate multiple choices with spaces):"
echo "1) Check Packages"
echo "2) Update KiCad"
echo "3) Update Dependencies"
echo "4) Update GHDL"
echo "5) Update Verilator"
echo "6) Update NGSPICE"
echo "Enter your choices (e.g., 1 3 5):"

# Read user input
read -a choices

# Execute selected scripts
for choice in "${choices[@]}"; do
    if [[ $choice -ge 1 && $choice -le 6 ]]; then
        script="${FILES[$((choice-1))]}"
        title="${TITLES[$((choice-1))]}"
        if [[ -f "$script" ]]; then
            echo "=============================="
            echo "$title..."
            echo "Running $script..."
            bash "$script"
        else
            echo "Error: $script not found!"
        fi
    else
        echo "Invalid choice: $choice"
    fi
done

echo "All selected updates completed."