# Git Repository Fetcher

A Qt-based graphical application for managing and automatically fetching multiple Git repositories.

## Features

- **Graphical Repository Management**: Add, edit, and remove Git repositories through an intuitive GUI
- **Scheduled Fetching**: Automatically fetch repositories at configurable intervals
- **Individual Repository Settings**: Each repository can have its own fetch interval and can be enabled/disabled
- **Real-time Status**: View the status of each repository and fetch operations
- **Activity Logging**: Comprehensive logging of all fetch operations with timestamps
- **Configuration Persistence**: All settings are saved to a JSON configuration file
- **libgit2 Integration**: Uses libgit2 library for fast, reliable Git operations without external dependencies
- **Automatic Cloning**: Automatically clones repositories if they don't exist locally

## Building

### Prerequisites
- CMake 3.16 or higher
- Qt 5 or Qt 6 with Widgets component
- libgit2 development libraries
- C++ compiler with C++17 support

### Build Steps
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Usage

### Adding a Repository
1. Click the "Add" button
2. Fill in the repository details:
   - **Name**: A friendly name for the repository
   - **URL**: The Git repository URL (e.g., `https://github.com/user/repo.git`)
   - **Local Path**: Where to store the repository locally
   - **Branch**: The branch to fetch (defaults to "main")
   - **Fetch Interval**: How often to fetch (in minutes)
   - **Enabled**: Whether this repository should be automatically fetched

### Managing Repositories
- **Edit**: Select a repository and click "Edit" to modify its settings
- **Remove**: Select a repository and click "Remove" to delete it
- **Fetch Selected**: Manually fetch the currently selected repository
- **Fetch All Now**: Manually fetch all enabled repositories

### Global Settings
- **Global Interval**: The base interval for the auto-fetch timer
- **Enable Auto Fetch**: Toggle automatic fetching on/off

### Activity Log
The right panel shows a real-time log of all operations, including:
- Repository additions/removals
- Fetch operations (success/failure)
- Configuration changes
- Error messages

## Configuration

The application stores its configuration in a JSON file located at:
- Linux: `~/.config/fetchdeeznuts/repositories.json`
- Windows: `%APPDATA%/fetchdeeznuts/repositories.json`
- macOS: `~/Library/Preferences/fetchdeeznuts/repositories.json`

## How It Works

1. **Scheduled Fetching**: The application uses a QTimer to periodically check if any repositories need to be fetched based on their individual intervals
2. **Git Operations**: Uses libgit2 library for direct Git operations without external process dependencies
3. **Automatic Cloning**: If a repository doesn't exist locally, it's automatically cloned before fetching
4. **Status Tracking**: Tracks the last fetch time and current status of each repository
5. **Error Handling**: Gracefully handles network errors, authentication failures, and other Git-related issues with detailed error messages

## Example Use Cases

- **Development Workflow**: Keep multiple project repositories up-to-date automatically
- **Backup Strategy**: Regularly fetch important repositories to ensure local backups
- **CI/CD Integration**: Monitor repository changes for automated builds
- **Team Collaboration**: Stay synchronized with team repositories

## Troubleshooting

- **libgit2 Not Found**: Install libgit2 development libraries:
  - Ubuntu/Debian: `sudo apt install libgit2-dev`
  - Arch Linux: `sudo pacman -S libgit2`
  - Fedora: `sudo dnf install libgit2-devel`
- **Permission Errors**: Make sure the application has write access to the local repository paths
- **Network Issues**: Check your internet connection and repository URLs
- **Authentication**: For private repositories, ensure your Git credentials are properly configured (SSH keys or HTTPS credentials)
- **Clone Failures**: Check that the repository URL is correct and accessible
