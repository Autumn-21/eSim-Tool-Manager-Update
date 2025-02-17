nghdl="nghdl-simulator"

function update_ngspice_json {
    local ngspice_version="$1"
    local install_date=$(date '+%Y-%m-%d %H:%M:%S')
    local json_file="../information.json"

    echo "Checking JSON file at: $json_file"
    
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
    
    ngspice_dir="$HOME/$nghdl"
    package_dir="./nghdl/packages"
    local script_dir="$(cd "$(dirname "$0")" && pwd)"
    
    echo "Removing previously installed NGSPICE (if any)"    
    sudo apt-get purge -y ngspice
    
    echo "Installing required dependencies..."
    sudo apt-get update
    sudo apt-get install -y build-essential autoconf automake libtool \
                            pkg-config bison flex libx11-dev libxaw7-dev \
                            libreadline-dev libfftw3-dev libngspice0-dev \
                            xorg-dev libjpeg-dev libpng-dev libfreetype6-dev
    
    mkdir -p "$ngspice_dir"
    rm -rf "$ngspice_dir"/*
    
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
    
    temp_extract_dir="$HOME/temp_ngspice"
    rm -rf "$temp_extract_dir"
    mkdir -p "$temp_extract_dir"
    tar -xzf "$package_dir/$version" -C "$temp_extract_dir" --strip-components=1
    
    mv "$temp_extract_dir"/* "$ngspice_dir"/
    rm -rf "$temp_extract_dir"
    
    cd "$ngspice_dir"
    
    mkdir -p install_dir
    mkdir -p release
    
    if [[ -f "configure.ac" ]]; then
        echo "Regenerating configure script..."
        sudo apt-get install -y libtool libreadline-dev
        libtoolize --force --copy
        aclocal
        autoheader
        automake --add-missing --foreign
        autoconf
        autoreconf -fi
    else
        echo "Error: configure.ac missing, cannot regenerate configure script."
        exit 1
    fi
    
    cd release
    
    echo "Configuring NGSPICE..........."
    sleep 2
    
    chmod +x ../configure
    ../configure --enable-xspice --disable-debug  --prefix="$ngspice_dir/install_dir/" --exec-prefix="$ngspice_dir/install_dir/"
    
    if [[ ! -f Makefile ]]; then
        echo "Error: Makefile not generated, stopping."
        exit 1
    fi
    
    make -j$(nproc)
    make install
    
    sudo chmod 755 "$ngspice_dir/install_dir/bin/ngspice"
    
    echo "Removing previously installed Ngspice (if any)"    
    sudo apt-get purge -y ngspice
    
    echo "NGSPICE updated successfully"
    
    sudo rm -f /usr/bin/ngspice
    sudo ln -sf "$ngspice_dir/install_dir/bin/ngspice" /usr/bin/ngspice
    echo "Added softlink for NGSPICE....."
    
    cd "$script_dir"
    ngspice_version=$(basename "$version" .tar.gz)
    update_ngspice_json "$ngspice_version"
    
    echo "Adding NGSPICE to PATH..."
    export PATH="$ngspice_dir/install_dir/bin:$PATH"
    echo 'export PATH="$HOME/nghdl-simulator/install_dir/bin:$PATH"' >> ~/.bashrc
    echo 'export PATH="$HOME/nghdl-simulator/install_dir/bin:$PATH"' >> ~/.profile
    
    source ~/.bashrc
    source ~/.profile
    
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