nghdl="nghdl-simulator"

function update_ngspice_json {
    local ngspice_version="$1"
    local install_date=$(date '+%Y-%m-%d %H:%M:%S')
    local json_file="../information.json"

    echo "Checking JSON file at: $json_file"
    echo "Current directory: $(pwd)"
    
    if [[ -z "$ngspice_version" ]]; then
        echo "Error: NGSPICE version is empty. JSON not updated."
        return 1
    fi

    if [[ ! -f "$json_file" ]]; then
        echo "JSON file not found. Creating a new one."
        echo '{ "important_packages": [ { "package_name": "ngspice", "version": "", "installed_date": "" } ] }' > "$json_file"
    fi

    jq --arg version "$ngspice_version" --arg date "$install_date" \
       '(.important_packages[] | select(.package_name == "ngspice")) |= (.version = $version | .installed_date = $date)' \
       "$json_file" > temp.json && mv temp.json "$json_file"

    echo "Updated NGSPICE version to $ngspice_version in $json_file"
}

function updateNGSPICE {
    echo "Updating NGSPICE........................................"
    
    # Define NGSPICE installation directory
    ngspice_dir="$HOME/$nghdl"
    package_dir="./nghdl/packages"
    local script_dir="$(cd "$(dirname "$0")" && pwd)"
    
    # Remove previous NGSPICE installation if any
    echo "Removing previously installed NGSPICE (if any)"    
    sudo apt-get purge -y ngspice
    
    # Create NGSPICE extraction directory in Home if not exists
    mkdir -p "$ngspice_dir"
    rm -rf "$ngspice_dir"/*
    
    # Prompt user to select NGSPICE version
    versions=("ngspice-38.tar.gz" "ngspice-40.tar.gz" "ngspice-43.tar.gz")
    echo "Please choose an NGSPICE version to install:"
    echo "1) ngspice-38"
    echo "2) ngspice-40"
    echo "3) ngspice-43"
    read -p "Enter the number corresponding to the version you want to install: " version_choice
    
    case $version_choice in
        1) version="ngspice-38.tar.gz" ;;
        2) version="ngspice-40.tar.gz" ;;
        3) version="ngspice-43.tar.gz" ;;
        *) echo "Invalid selection. Exiting."; exit 1 ;;
    esac
    
    echo "Selected version: $version"
    
    if [[ ! -f "$package_dir/$version" ]]; then
        echo "Error: File $package_dir/$version not found!"
        exit 1
    fi
    
    # Extract NGSPICE to Home Directory
    temp_extract_dir="$HOME/temp_ngspice"
    rm -rf "$temp_extract_dir"
    mkdir -p "$temp_extract_dir"
    tar -xzf "$package_dir/$version" -C "$temp_extract_dir"
    
    extracted_dir=$(find "$temp_extract_dir" -mindepth 1 -maxdepth 1 -type d | head -n 1)
    
    if [[ -z "$extracted_dir" ]]; then
        echo "Error: Extraction failed!"
        exit 1
    fi
    
    # Handle nested directories by moving the contents
    while [[ $(find "$extracted_dir" -mindepth 1 -maxdepth 1 -type d | wc -l) -eq 1 ]]; do
        extracted_dir=$(find "$extracted_dir" -mindepth 1 -maxdepth 1 -type d | head -n 1)
    done
    
    mv "$extracted_dir"/* "$ngspice_dir"/
    rm -rf "$temp_extract_dir"
    
    # Change to NGSPICE directory
    cd "$ngspice_dir"
    
    # Make local install and release directories
    mkdir -p "$ngspice_dir/install_dir"
    mkdir -p "$ngspice_dir/release"
    cd "$ngspice_dir/release"
    
    echo "Configuring NGSPICE..........."
    sleep 2
    
    if [[ ! -f "../configure" ]]; then
        echo "'configure' script not found! Running autoreconf..."
        cd "$ngspice_dir"
        if [[ -f "configure.ac" ]]; then
            autoreconf -fi
        else
            echo "Error: 'configure.ac' not found! Cannot generate configure script."
            exit 1
        fi
        cd "$ngspice_dir/release"
    fi
    
    chmod +x ../configure
    ../configure --enable-xspice --disable-debug --prefix="$ngspice_dir/install_dir/" --exec-prefix="$ngspice_dir/install_dir/"
    
    make -j$(nproc)
    make install
    
    # Make it executable
    if [[ ! -f "$ngspice_dir/install_dir/bin/ngspice" ]]; then
        echo "Error: NGSPICE binary not found after installation!"
        exit 1
    fi
    sudo chmod 755 "$ngspice_dir/install_dir/bin/ngspice"
    
    echo "NGSPICE updated successfully"
    echo "Updating symlink for NGSPICE..."
    
    # Remove old symlink and create new one
    sudo rm -f /usr/bin/ngspice
    sudo ln -sf "$ngspice_dir/install_dir/bin/ngspice" /usr/bin/ngspice
    echo "Added softlink for NGSPICE....."
    
    # Go back to script directory before updating JSON
    cd "$script_dir"
    echo "Current directory before JSON update: $(pwd)"
    
    # Update JSON file
    ngspice_version=$(basename "$version" .tar.gz)
    update_ngspice_json "$ngspice_version"

    # Add NGSPICE to PATH permanently
    echo "Adding NGSPICE to PATH..."
    export PATH="$ngspice_dir/install_dir/bin:$PATH"
    echo 'export PATH="$HOME/nghdl-simulator/install_dir/bin:$PATH"' >> ~/.bashrc
    echo 'export PATH="$HOME/nghdl-simulator/install_dir/bin:$PATH"' >> ~/.profile

    # Reload shell configuration to apply changes
    source ~/.bashrc
    source ~/.profile

    # Verify NGSPICE installation and display version
    echo "Verifying NGSPICE installation..."
    if command -v ngspice &> /dev/null; then
        echo "NGSPICE successfully installed!"
        echo "Installed NGSPICE version:"
        ngspice --version
    else
        echo "Error: NGSPICE installation not found!"
        exit 1
    fi
}

updateNGSPICE
