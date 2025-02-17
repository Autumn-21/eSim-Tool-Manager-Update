#!/bin/bash

updateKicad() {
    echo "Select the KiCad version to install (7.0.11 or 8.0.8):"
    read -p "Enter 7 or 8: " kicad_version

    if [[ "$kicad_version" != "7" && "$kicad_version" != "8" ]]; then
        echo "Invalid selection. Please enter 7 or 8."
        return 1
    fi

    echo "Removing all existing KiCad versions and related packages..."
    sudo apt-get remove --purge -y kicad kicad-footprints kicad-libraries kicad-symbols kicad-templates
    sudo apt-get autoremove -y
    sudo apt-get autoclean

    if [ -d ~/.config/kicad/7.0 ]; then
        echo "Removing KiCad 7.0 configuration folder..."
        rm -rf ~/.config/kicad/7.0
    fi

    if [ -d ~/.config/kicad/8.0 ]; then
        echo "Removing KiCad 8.0 configuration folder..."
        rm -rf ~/.config/kicad/8.0
    fi

    if [[ "$kicad_version" == "7" ]]; then
        kicadppa="kicad/kicad-7.0-releases"
        version="7.0.11~ubuntu20.04.1"
    else
        kicadppa="kicad/kicad-8.0-releases"
        version="8.0.8-0~ubuntu20.04.1"
    fi

    echo "Checking for existing KiCad PPAs..."
    sudo add-apt-repository --remove -y ppa:kicad/kicad-6.0-releases 2>/dev/null
    sudo add-apt-repository --remove -y ppa:kicad/kicad-7.0-releases 2>/dev/null
    sudo add-apt-repository --remove -y ppa:kicad/kicad-8.0-releases 2>/dev/null

    echo "Adding KiCad PPA ($kicadppa)..."
    sudo add-apt-repository -y ppa:$kicadppa
    sudo apt-get update

    echo "Installing KiCad version $version..."
    sudo apt-get install -y --no-install-recommends kicad="$version" || {
        echo "Error installing KiCad core package. Trying to install the latest available version..."
        sudo apt-get install -y --no-install-recommends kicad
    }

    echo "Installing additional KiCad libraries..."
    sudo apt-get install -y --no-install-recommends kicad-footprints kicad-libraries kicad-symbols kicad-templates || {
        echo "Warning: Some KiCad libraries might not be available for this version."
    }

    echo "Verifying installation..."
    if command -v kicad &> /dev/null; then
        installed_version=$(dpkg -l | grep kicad | awk '{print $3}' | head -n 1)
        
        if [[ -z "$installed_version" ]]; then
            echo "Error: Unable to determine KiCad version. JSON not updated."
            return 1
        fi

        echo "KiCad installation successful! Installed version: $installed_version"
        update_kicad_json "$installed_version"
    else
        echo "Error: KiCad was not installed correctly."
        return 1
    fi
}

update_kicad_json() {
    json_file="./information.json"
    kicad_version="$1"
    install_date=$(date '+%Y-%m-%d %H:%M:%S')

    if [[ -z "$kicad_version" ]]; then
        echo "Error: KiCad version is empty. JSON not updated."
        return 1
    fi

    jq --arg version "$kicad_version" --arg date "$install_date" \
       '(.important_packages[] | select(.package_name == "kicad")) |= (.version = $version | .installed_date = $date)' \
       "$json_file" > temp.json && mv temp.json "$json_file"

    echo "Updated KiCad version to $kicad_version in $json_file"
}

function copyKicadLibrary
{
    echo "Extracting custom KiCad library..."
    tar -xJf library/kicadLibrary.tar.xz

    # Check for the correct KiCad version (6.0/7.0/8.0) configuration folder
    if [ -d "$HOME/.config/kicad/6.0" ]; then
        echo "KiCad 6 configuration folder already exists."
        kicad_config_dir="$HOME/.config/kicad/6.0"
    elif [ -d "$HOME/.config/kicad/7.0" ]; then
        echo "KiCad 7 configuration folder already exists."
        kicad_config_dir="$HOME/.config/kicad/7.0"
    elif [ -d "$HOME/.config/kicad/8.0" ]; then
        echo "KiCad 8 configuration folder already exists."
        kicad_config_dir="$HOME/.config/kicad/8.0"
    else
        echo "No KiCad config folder found. Creating a new one for KiCad 6..."
        mkdir -p "$HOME/.config/kicad/6.0"
        kicad_config_dir="$HOME/.config/kicad/6.0"
    fi

    # Copy symbol table for eSim custom symbols
    cp kicadLibrary/template/sym-lib-table "$kicad_config_dir/"
    echo "Symbol table copied to the directory: $kicad_config_dir"

    # Copy KiCad symbols made for eSim
    sudo cp -r kicadLibrary/eSim-symbols/* /usr/share/kicad/symbols/

    set +e      # Temporarily disable exit on error
    trap "" ERR # Do not trap on error of any command
    
    # Remove extracted KiCad Library (no longer needed)
    rm -rf kicadLibrary

    set -e      # Re-enable exit on error
    trap error_exit ERR

    # Change ownership from Root to the User for the copied symbols
    sudo chown -R $USER:$USER /usr/share/kicad/symbols/

    echo "Library successfully copied and ownership adjusted."
}

# Run the update function
updateKicad
copyKicadLibrary