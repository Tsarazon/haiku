# Complete Haiku Codebase Migration to GitLab

## Repository Overview

**Current Repository**: 5.9GB Haiku OS codebase with buildtools
- **Source**: Your fork at https://github.com/Tsarazon/haiku.git
- **Upstream**: Official Haiku repository at https://github.com/haiku/haiku.git
- **Structure**: Full OS codebase with submodules (buildtools)
- **Status**: Contains your CMake migration work + official Haiku code

## Migration Strategy Options

### Option 1: Full Codebase Migration (Recommended)
Migrate the entire Haiku repository including your modifications to private GitLab.

**Advantages:**
- ✅ Complete development environment
- ✅ Full history preservation  
- ✅ All your CMake work included
- ✅ Can continue OS development
- ✅ Private space for experimental work

**Considerations:**
- 🔶 5.9GB repository size
- 🔶 Need GitLab storage space
- 🔶 Submodule handling required

### Option 2: CMake Migration Fork
Create focused repository with just CMake migration work.

**Advantages:**
- ✅ Smaller size (~100MB)
- ✅ Focused on migration work
- ✅ Faster clones/pushes

**Disadvantages:**
- ❌ No full OS context
- ❌ Limited to build system work

## Recommended Approach: Full Migration

### Step 1: GitLab Repository Setup

#### Repository Configuration
```
Name: haiku-os-development
Visibility: Private
Description: Haiku OS development with CMake build system migration
```

#### GitLab Storage Requirements
- **Repository**: 5.9GB (current size)
- **GitLab Free Tier**: 10GB limit per project
- **Status**: ✅ Fits within free tier limits

### Step 2: Prepare Repository for Migration

#### Clean Current State
```bash
cd /home/ruslan/haiku

# Check what's staged/unstaged
git status

# Check current branch and commits
git log --oneline -10

# Check submodules
git submodule status
```

#### Handle Generated Files (Optional Cleanup)
```bash
# These can be excluded to reduce size:
du -sh generated/     # Build artifacts
du -sh build/         # CMake build dirs  
du -sh src/kits/build/ # Our CMake build
```

### Step 3: Migration Commands

#### Create GitLab Repository
1. Go to https://gitlab.com
2. Create new private project: "haiku-os-development"
3. Don't initialize with README (we have existing repo)

#### Migrate Full Repository
```bash
cd /home/ruslan/haiku

# Add GitLab remote
git remote add gitlab https://gitlab.com/[YOUR_USERNAME]/haiku-os-development.git

# Push all branches and tags
git push gitlab --all
git push gitlab --tags

# Push submodules (buildtools)
git submodule foreach git push gitlab --all

# Set GitLab as default remote
git remote set-url origin https://gitlab.com/[YOUR_USERNAME]/haiku-os-development.git
```

#### Alternative: Clean Migration (Exclude Generated Files)
```bash
# Create .gitignore for generated content
echo "generated/" >> .gitignore
echo "build/" >> .gitignore
echo "src/kits/build/" >> .gitignore
echo "*.so" >> .gitignore
echo "*.o" >> .gitignore

# Commit cleanup
git add .gitignore
git commit -m "Add gitignore for generated files before GitLab migration"

# Then push to GitLab
git push gitlab --all
```

### Step 4: GitLab Configuration

#### Repository Settings
```yaml
Repository Settings:
  - Visibility: Private
  - Issues: Enabled
  - Wiki: Enabled  
  - Merge Requests: Enabled
  - CI/CD: Enabled

Branch Protection:
  - Protect master branch
  - Require merge request approval
  - Enable CI pipeline checks
```

#### CI/CD Pipeline (.gitlab-ci.yml)
```yaml
stages:
  - validate
  - build-cmake
  - build-jam
  - test

variables:
  HAIKU_ROOT: "/builds/[username]/haiku-os-development"

validate-cmake:
  stage: validate
  script:
    - cmake --version
    - python3 --version
    - ls -la src/kits/

build-cmake-kits:
  stage: build-cmake
  script:
    - cd src/kits
    - cmake -B build .
    - make -C build all
    - ls -la build/*.so
  artifacts:
    paths:
      - src/kits/build/*.so
    expire_in: 1 week

build-jam-baseline:
  stage: build-jam  
  script:
    - ./buildtools/jam/bin.linuxx86/jam libbe.so
  artifacts:
    paths:
      - generated/objects/*/kits/libbe.so
    expire_in: 1 week

compare-libraries:
  stage: test
  dependencies:
    - build-cmake-kits
    - build-jam-baseline
  script:
    - ls -la src/kits/build/*.so generated/objects/*/kits/libbe.so
    - echo "Size comparison:"
    - stat --format="%s %n" src/kits/build/*.so generated/objects/*/kits/libbe.so
```

### Step 5: Post-Migration Setup

#### Update Development Workflow
```bash
# Clone from GitLab (for team members)
git clone https://gitlab.com/[username]/haiku-os-development.git
cd haiku-os-development

# Initialize submodules
git submodule update --init --recursive

# Set up CMake builds
cd src/kits
cmake -B build .
make -C build
```

#### Project Management Setup
- **Issues**: Import current TODO list as GitLab issues
- **Milestones**: Create migration phases as milestones
- **Labels**: CMake, Jam, BuildSystem, Migration, Testing
- **Wiki**: Import all documentation files

### Step 6: Team Collaboration Setup

#### Access Control
```
Repository Access:
  - Owner: You (full access)
  - Developers: Push/Merge access to develop branches
  - Reviewers: Merge request approval rights
```

#### Merge Request Templates
```markdown
## CMake Migration Merge Request

### Changes Made
- [ ] Kit objects modified
- [ ] CMake configuration updated
- [ ] Documentation updated
- [ ] Tests passing

### Testing Completed
- [ ] Kit builds successfully
- [ ] Library comparison tests pass
- [ ] No regressions in functionality

### Migration Phase
- [ ] Phase 3A: Simple Applications
- [ ] Phase 3B: Libraries  
- [ ] Phase 3C: Core Components
- [ ] Phase 4: System Servers
```

## Migration Commands Summary

**Quick Migration (Full Repository):**
```bash
cd /home/ruslan/haiku
git remote add gitlab https://gitlab.com/[username]/haiku-os-development.git
git push gitlab --all
git push gitlab --tags
git remote set-url origin https://gitlab.com/[username]/haiku-os-development.git
```

**Clean Migration (Exclude Build Artifacts):**
```bash
cd /home/ruslan/haiku

# Clean up generated files first
rm -rf generated/objects build src/kits/build

# Add gitignore
echo -e "generated/\nbuild/\n*.so\n*.o" >> .gitignore
git add .gitignore
git commit -m "Prepare for GitLab migration: exclude generated files"

# Push to GitLab
git remote add gitlab https://gitlab.com/[username]/haiku-os-development.git
git push gitlab master
git push gitlab --tags
```

## Benefits of Full Codebase Migration

1. **Complete Development Environment**: Full Haiku OS with your modifications
2. **Private Experimentation**: Work on sensitive changes privately
3. **Team Collaboration**: GitLab's superior collaboration tools
4. **CI/CD Integration**: Automated testing of both Jam and CMake builds
5. **Project Management**: Issues, milestones, merge requests
6. **Documentation**: Wiki for extended documentation
7. **Backup**: Complete backup of your Haiku work
8. **Future Proofing**: Own the complete development environment

The full migration preserves all your work while providing a professional development platform for continued Haiku OS development and CMake migration work.