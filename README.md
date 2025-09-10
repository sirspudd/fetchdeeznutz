# Git Repository Fetcher

A Qt-based graphical application for managing and automatically fetching multiple Git repositories.

This application largely written by Cursor with me acting as product manager. I have reviewed very little of the code so tread lightly, and use at your own risk.

## Known issues

* It was written by language model
* the commit counts are horribly wrong and the app opts to show change delta from upstream by summing all the deltas between our repo and all its configured remotes.

## Time investment

* product development: 4 hours
* QA: 1 hour
* software dev: 40 min

## Logo

original logo was meant to be a collection of nuts

current logo .... well here is the prompt/interation

me: Could I please get a better logo for the project; your nuts image is pretty terrible

cursor: You're absolutely right! Let me create a much better, more professional logo for FetchDeezNutz. I'll design something that's more polished and appropriate for a Git repository manager.

:Cursor:

✅ Professional Logo Design Complete
🎨 New Logo Features:
1. Professional Design Elements:
Modern Color Scheme: Deep blue background with orange accent
Clean Typography: Sharp, geometric shapes
Professional Gradients: Subtle gradients for depth and modern appeal
Consistent Styling: Cohesive design language throughout
2. Git-Focused Concept:
Central Nut: Represents the "nuts" part of the name with a clean hexagonal design
Fetch Arrows: Four directional arrows representing Git fetch operations
Repository Indicators: Small dots showing active/inactive repositories
Connection Lines: Visual connections between the nut and fetch operations
3. Technical Excellence:
Scalable Vector: SVG format for perfect scaling at any size
Multiple Sizes: Generated PNG versions for all standard icon sizes
High Quality: Crisp rendering at all resolutions
Professional Appearance: Suitable for business and development environments
🎯 Design Philosophy:
Color Palette:
Primary Blue: #1e3a8a to #1e40af (professional, trustworthy)
Accent Orange: #f97316 to #ea580c (energetic, Git-inspired)
White Elements: Clean arrows and connections
Status Colors: Green for active, gray for inactive repositories
Visual Elements:
Hexagonal Nut: Modern, technical appearance
Directional Arrows: Clear representation of fetch operations
Repository Dots: Visual status indicators
Subtle Shadows: Depth and professionalism
�� Files Created:
fetchdeeznuts_logo_v3.svg: The final professional logo
nuts_icon.svg: Updated with the new design
PNG Icons: All standard sizes (16x16 to 256x256)
Alternative Versions: fetchdeeznuts_logo.svg and fetchdeeznuts_logo_v2.svg for comparison
🔧 Integration:
Application Integration:
System Tray: Uses the new professional logo
Desktop File: References the updated icon
CMake Build: Automatically generates PNG versions
Resource System: Embedded in Qt resource system
Installation Ready:
Desktop Integration: Proper icon theme integration
Multiple Formats: SVG and PNG for maximum compatibility
Standard Locations: Follows Linux desktop standards
Professional Appearance: Suitable for any desktop environment
🎨 Logo Evolution:
Before (Old Design):
Basic brown nuts scattered around
No clear Git concept
Amateur appearance
Poor scalability
After (New Design):
Professional blue and orange color scheme
Clear Git fetch concept with arrows
Modern, clean design
Perfect scalability and clarity
�� Benefits:
✅ Professional Appearance: Suitable for business environments
✅ Clear Concept: Immediately communicates Git fetching functionality
✅ Modern Design: Follows current design trends
✅ Technical Excellence: Perfect rendering at all sizes
✅ Brand Recognition: Memorable and distinctive
✅ Cross-Platform: Works beautifully on all desktop environments
The new logo transforms FetchDeezNutz from having an amateur appearance to looking like a professional, enterprise-ready Git repository management tool. The design clearly communicates the application's purpose while maintaining the playful "nuts" reference in a sophisticated way!

## Features

- **Graphical Repository Management**: Add, edit, and remove Git repositories through an intuitive GUI
- **Scheduled Fetching**: Automatically fetch repositories at configurable intervals
- **Individual Repository Settings**: Each repository can have its own fetch interval and can be enabled/disabled
- **Real-time Status**: View the status of each repository and fetch operations
- **Activity Logging**: Comprehensive logging of all fetch operations with timestamps
- **Configuration Persistence**: All settings are saved to a JSON configuration file
- **libgit2 Integration**: Uses libgit2 library for fast, reliable Git operations without external dependencies
- **Directory Scanning**: Automatically discover and add all Git repositories in a directory tree
- **Smart Repository Detection**: Recursively scans directories while avoiding common build/cache directories
- **Multiple Remotes Support**: Fetch from all configured remotes (origin, upstream, fork, etc.) for each repository
- **Individual Remote Status**: Track the status and last fetch time for each remote separately
- **Commit Count Deltas**: Display how many commits ahead/behind each repository is compared to its remotes
- **Real-time Status Updates**: Commit counts are automatically calculated after each fetch operation
- **Hierarchical Tree View**: Repositories are organized by filesystem path in an expandable tree structure
- **Path-based Organization**: Easily see the directory structure of your repositories at a glance

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

### Repository Tree View
The application displays repositories in a hierarchical tree structure organized by filesystem path:

```
📁 /home/user/projects
  ├── 📁 frontend
  │   ├── ● my-react-app - Success (main) [1 remotes] [up-to-date]
  │   └── ● vue-project - Success (main) [2 remotes] [+3/-1]
  ├── 📁 backend
  │   ├── ● api-server - Success (main) [1 remotes] [-2]
  │   └── ● database - Success (main) [1 remotes] [+1]
  └── 📁 tools
      └── ● build-scripts - Success (main) [1 remotes] [up-to-date]
```

### Understanding Commit Count Deltas
The repository tree shows commit count deltas next to each repository:
- **[+5]**: 5 commits ahead of remote (local changes not pushed)
- **[-3]**: 3 commits behind remote (remote changes not pulled)
- **[+2/-1]**: 2 commits ahead, 1 commit behind (diverged branches)
- **[up-to-date]**: Local and remote are synchronized
- **[-0]**: No commits behind (up-to-date with remote)

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
4. **Repository Validation**: Only works with existing Git repositories - repositories must be cloned manually before adding to the application
5. **Status Tracking**: Tracks the last fetch time and current status for each repository and each remote
6. **Commit Count Analysis**: Automatically calculates and displays how many commits each repository is ahead/behind its remotes
7. **Error Handling**: Gracefully handles network errors, authentication failures, and other Git-related issues with detailed error messages
8. **Partial Success Handling**: If some remotes fail to fetch, the operation is marked as "Partial" with details about which remotes failed

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
- **Repository Not Found**: Make sure the local path points to an existing Git repository that has been cloned manually
- **Permission Errors**: Make sure the application has write access to the local repository paths
- **Network Issues**: Check your internet connection and repository URLs
- **Authentication**: For private repositories, ensure your Git credentials are properly configured (SSH keys or HTTPS credentials)
