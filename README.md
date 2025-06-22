# Maomaowm

This project's development is based on [dwl](https://codeberg.org/dwl/dwl/).

"Since many people have asked about the meaning of this compositor's name, 'Maomao' is an online alias I've been using for years - it comes from the first two characters of the Chinese word for 'caterpillar' (毛毛虫). You can basically think of it as meaning 'caterpillar'.

1. **Lightweight & Fast Build**  
   - *Maomao* is as lightweight as *dwl*, and its build can be completed within few seconds. Despite this, *maomao* does not compromise on functionality.  

2. **Feature Highlights**  
   - In addition to basic WM functionality, Maomao provides:
     - Base tag not workspace (supports separate window layouts for each tag)
     - Smooth and customizable complete animations (window open/move/close, tag enter/leave)
     - Excellent input method support (text input v2/v3)
     - Flexible window layouts with easy switching (scroller, master, monocle, spiral, etc.)
     - Rich window states (swallow, minimize, maximize, unglobal, global, fakefullscreen, overlay, etc.)
     - Simple yet powerful external configuration
     - Sway-like scratchpad and named scratchpad
     - Minimize window to scratchpad
     - Hycov-like overview

Master-Stack Layout

https://github.com/user-attachments/assets/a9d4776e-b50b-48fb-94ce-651d8a749b8a

Scroller Layout

https://github.com/user-attachments/assets/c9bf9415-fad1-4400-bcdc-3ad2d76de85a

# Supported layouts
  - Tile
  - Scroller  
  - Monocle
  - Grid
  - Dwindle
  - Spiral
  - Deck

# Installation

## Dependencies

```bash
yay -S glibc wayland libinput libdrm pixman libxkbcommon git meson ninja wayland-protocols libdisplay-info libliftoff hwdata seatd pcre2
```

## Arch Linux
```bash
yay -S maomaowm-git

```

## Other
```bash
# wlroots 0.19.0 release with a fix-patch to avoid crash
git clone -b 0.19.0-fix  https://github.com/DreamMaoMao/wlroots.git
cd wlroots
meson build -Dprefix=/usr
sudo ninja -C build install

git clone https://github.com/DreamMaoMao/maomaowm.git
cd maomaowm
meson build -Dprefix=/usr
sudo ninja -C build install
```

## Suggested Tools

```
yay -S rofi foot xdg-desktop-portal-wlr swaybg waybar wl-clip-persist cliphist wl-clipboard wlsunset xfce-polkit swaync

```

## Some Common Default Keybindings
- alt+return: open foot terminal
- alt+q: kill client
- alt+left/right/up/down: focus direction
- super+m: quit maomao

## My Dotfiles
- Dependencies
```
yay -S pamixer lavalauncher-mao-git wlr-dpms sway-audio-idle-inhibit-git swayidle dimland-git brightnessctl swayosd wlr-randr grim slurp satty swaylock-effects-git wlogout
```
### Maomao Config
[maomao-config](https://github.com/DreamMaoMao/dotfile/tree/main/maomao)
#### Other Tools
[foot](https://github.com/DreamMaoMao/dotfile/tree/main/foot)
[swaylock](https://github.com/DreamMaoMao/dotfile/tree/main/swaylock)
[wlogout](https://github.com/DreamMaoMao/dotfile/tree/main/wlogout)
[swaync](https://github.com/DreamMaoMao/dotfile/tree/main/swaync)

## Config Document
Refer to the [wiki](https://github.com/DreamMaoMao/maomaowm/wiki/)


# NixOS + Home-manager

The repo contains a flake that provides a NixOS module and a home-manager module for maomaowm.
Use the NixOS module to install maomaowm with other necessary components of a working Wayland environment.
Use the home-manager module to declare configuration and autostart for maomaowm.

Here's an example of using the modules in a flake:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    home-manager = {
      url = "github:nix-community/home-manager";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    flake-parts.url = "github:hercules-ci/flake-parts";
    maomaowm.url = "github:DreamMaoMao/maomaowm";
  };
  outputs =
    inputs@{ self, flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      debug = true;
      systems = [ "x86_64-linux" ];
      flake = {
        nixosConfigurations = {
          hostname = inputs.nixpkgs.lib.nixosSystem {
            system = "x86_64-linux";
            modules = [
              inputs.home-manager.nixosModules.home-manager

              # Add maomaowm nixos module
              inputs.maomaowm.nixosModules.maomaowm
              {
                programs.maomaowm.enable = true;
              }
              {
                home-manager = {
                  useGlobalPkgs = true;
                  useUserPackages = true;
                  backupFileExtension = "backup";
                  users."username".imports =
                    [
                      (
                        { ... }:
                        {
                          wayland.windowManager.maomaowm = {
                            enable = true;
                            settings = ''
                              # see config.conf
                            '';
                            autostart_sh = ''
                              # see autostart.sh
                              # Note: here no need to add shebang
                            '';
                          };
                        }
                      )
                    ]
                    ++ [
                      # Add maomaowm hm module
                      inputs.maomaowm.hmModules.maomaowm
                    ];
                };
              }
            ];
          };
        };
      };
    };
}
```

# Thanks to These Reference Repositories

- https://gitlab.freedesktop.org/wlroots/wlroots - Implementation of Wayland protocol

- https://github.com/dqrk0jeste/owl - Basal window animation

- https://codeberg.org/dwl/dwl - Basal dwl feature

- https://github.com/swaywm/sway - Sample of Wayland protocol
