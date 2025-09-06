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
- **Directory Scanning**: Automatically discover and add all Git repositories in a directory tree
- **Smart Repository Detection**: Recursively scans directories while avoiding common build/cache directories
- **Multiple Remotes Support**: Fetch from all configured remotes (origin, upstream, fork, etc.) for each repository
- **Individual Remote Status**: Track the status and last fetch time for each remote separately

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

### Adding Repositories

#### Adding a Single Repository
1. Click the "Add Repo" button
2. Fill in the repository details:
   - **Name**: A friendly name for the repository
   - **Local Path**: Where to store the repository locally
   - **Branch**: The branch to fetch (defaults to "main")
   - **Fetch Interval**: How often to fetch (in minutes)
   - **Enabled**: Whether this repository should be automatically fetched
   - **Remotes**: Add one or more Git remotes:
     - **Remote Name**: The name of the remote (e.g., "origin", "upstream", "fork")
     - **Remote URL**: The Git repository URL (e.g., `https://github.com/user/repo.git`)
     - Use "Add Remote" to add additional remotes
     - Use "Remove Remote" to remove selected remotes

#### Adding Multiple Repositories from a Directory
1. Click the "Add Directory" button
2. Select a top-level directory containing Git repositories
3. The application will automatically:
   - Recursively scan the directory tree
   - Discover all Git repositories (directories containing `.git` folders)
   - Extract repository information (name, all remotes, current branch)
   - Add them to the repository list with default settings
   - Skip directories that are already in the list
   - Avoid scanning common build/cache directories (`.git`, `node_modules`, `.vscode`, `.idea`, `build`, `dist`, `target`, `__pycache__`)

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
3. **Multiple Remote Fetching**: For each repository, fetches from all configured remotes (origin, upstream, fork, etc.)
4. **Automatic Cloning**: If a repository doesn't exist locally, it's automatically cloned using the first remote
5. **Status Tracking**: Tracks the last fetch time and current status for each repository and each remote
6. **Error Handling**: Gracefully handles network errors, authentication failures, and other Git-related issues with detailed error messages
7. **Partial Success Handling**: If some remotes fail to fetch, the operation is marked as "Partial" with details about which remotes failed

## Example Use Cases

- **Development Workflow**: Keep multiple project repositories up-to-date automatically
- **Workspace Management**: Scan your entire development workspace to manage all Git repositories at once
- **Fork Management**: Keep your forks synchronized with upstream repositories automatically
- **Backup Strategy**: Regularly fetch important repositories to ensure local backups
- **CI/CD Integration**: Monitor repository changes for automated builds
- **Team Collaboration**: Stay synchronized with team repositories and upstream sources
- **Multi-Project Development**: Manage repositories across multiple projects and organizations
- **Open Source Contribution**: Automatically fetch from both your fork and the upstream repository

## Troubleshooting

- **libgit2 Not Found**: Install libgit2 development libraries:
  - Ubuntu/Debian: `sudo apt install libgit2-dev`
  - Arch Linux: `sudo pacman -S libgit2`
  - Fedora: `sudo dnf install libgit2-devel`
- **Permission Errors**: Make sure the application has write access to the local repository paths
- **Network Issues**: Check your internet connection and repository URLs
- **Authentication**: For private repositories, ensure your Git credentials are properly configured (SSH keys or HTTPS credentials)
- **Clone Failures**: Check that the repository URL is correct and accessible
