#!/usr/bin/env python3
"""
ISO Image Comparison Tool
Compares two ISO images across multiple parameters and generates a detailed report
"""

import os
import sys
import hashlib
import subprocess
import tempfile
import json
import datetime
import struct
import time
import re
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import argparse

class ISOComparator:
    def __init__(self, iso1_path: str, iso2_path: str):
        self.iso1 = Path(iso1_path).absolute()
        self.iso2 = Path(iso2_path).absolute()
        self.report = {
            'timestamp': datetime.datetime.now().isoformat(),
            'iso1': str(self.iso1),
            'iso2': str(self.iso2),
            'comparisons': {}
        }
        
    def run_command(self, cmd: List[str], check: bool = True) -> Tuple[int, str, str]:
        """Run shell command and return exit code, stdout, stderr"""
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=check)
            return result.returncode, result.stdout, result.stderr
        except subprocess.CalledProcessError as e:
            return e.returncode, e.stdout, e.stderr
        except Exception as e:
            return -1, "", str(e)
    
    def get_file_size(self) -> Dict:
        """Compare file sizes"""
        size1 = self.iso1.stat().st_size
        size2 = self.iso2.stat().st_size
        
        return {
            'iso1_size': size1,
            'iso2_size': size2,
            'size_diff': size2 - size1,
            'size_diff_mb': (size2 - size1) / (1024 * 1024),
            'size_diff_percent': ((size2 - size1) / size1 * 100) if size1 > 0 else 0
        }
    
    def get_checksums(self) -> Dict:
        """Calculate and compare checksums"""
        checksums = {}
        
        for iso, path in [('iso1', self.iso1), ('iso2', self.iso2)]:
            md5 = hashlib.md5()
            sha256 = hashlib.sha256()
            
            with open(path, 'rb') as f:
                for chunk in iter(lambda: f.read(8192), b''):
                    md5.update(chunk)
                    sha256.update(chunk)
            
            checksums[f'{iso}_md5'] = md5.hexdigest()
            checksums[f'{iso}_sha256'] = sha256.hexdigest()
        
        checksums['identical'] = checksums['iso1_sha256'] == checksums['iso2_sha256']
        return checksums
    
    def get_iso_info(self) -> Dict:
        """Extract ISO metadata using isoinfo"""
        info = {}
        
        for iso, path in [('iso1', self.iso1), ('iso2', self.iso2)]:
            cmd = ['isoinfo', '-d', '-i', str(path)]
            rc, stdout, stderr = self.run_command(cmd, check=False)
            
            if rc == 0:
                iso_data = {}
                for line in stdout.split('\n'):
                    if ':' in line:
                        key, value = line.split(':', 1)
                        iso_data[key.strip()] = value.strip()
                info[iso] = iso_data
            else:
                info[iso] = {'error': stderr or 'Failed to read ISO info'}
        
        return info
    
    def extract_file_list(self, iso_path: Path) -> Optional[List[str]]:
        """Extract list of files from ISO"""
        with tempfile.TemporaryDirectory() as tmpdir:
            mount_point = Path(tmpdir) / 'mount'
            mount_point.mkdir()
            
            # Try to mount ISO
            rc, _, _ = self.run_command(
                ['sudo', 'mount', '-o', 'loop,ro', str(iso_path), str(mount_point)],
                check=False
            )
            
            if rc != 0:
                # Fallback to isoinfo
                cmd = ['isoinfo', '-l', '-i', str(iso_path)]
                rc, stdout, _ = self.run_command(cmd, check=False)
                if rc == 0:
                    return stdout.split('\n')
                return None
            
            # Get file list
            files = []
            for root, dirs, filenames in os.walk(mount_point):
                for name in filenames + dirs:
                    rel_path = os.path.relpath(os.path.join(root, name), mount_point)
                    files.append(rel_path)
            
            # Unmount
            self.run_command(['sudo', 'umount', str(mount_point)], check=False)
            return sorted(files)
    
    def compare_file_structure(self) -> Dict:
        """Compare file structure of ISOs"""
        files1 = self.extract_file_list(self.iso1)
        files2 = self.extract_file_list(self.iso2)
        
        if files1 is None or files2 is None:
            return {
                'error': 'Could not extract file lists',
                'iso1_extracted': files1 is not None,
                'iso2_extracted': files2 is not None
            }
        
        set1 = set(files1)
        set2 = set(files2)
        
        return {
            'iso1_file_count': len(files1),
            'iso2_file_count': len(files2),
            'files_only_in_iso1': sorted(list(set1 - set2))[:100],  # Limit to 100 for report
            'files_only_in_iso2': sorted(list(set2 - set1))[:100],
            'common_files': len(set1 & set2),
            'total_unique_files': len(set1 | set2)
        }
    
    def analyze_boot_sector(self) -> Dict:
        """Analyze boot sector differences"""
        boot_info = {}
        
        for iso, path in [('iso1', self.iso1), ('iso2', self.iso2)]:
            # Read first 2KB of ISO
            with open(path, 'rb') as f:
                boot_sector = f.read(2048)
            
            # Check for common boot signatures
            signatures = {
                'el_torito': b'EL TORITO SPECIFICATION',
                'isolinux': b'ISOLINUX',
                'grub': b'GRUB',
                'efi': b'EFI PART',
                'haiku': b'haiku',
                'bootman': b'BOOTMAN'
            }
            
            found_signatures = []
            for name, sig in signatures.items():
                if sig.lower() in boot_sector.lower():
                    found_signatures.append(name)
            
            boot_info[iso] = {
                'signatures': found_signatures,
                'boot_sector_hash': hashlib.sha256(boot_sector).hexdigest()
            }
        
        boot_info['boot_sectors_identical'] = (
            boot_info['iso1']['boot_sector_hash'] == 
            boot_info['iso2']['boot_sector_hash']
        )
        
        return boot_info
    
    def analyze_haiku_packages(self) -> Dict:
        """Extract and compare Haiku packages if possible"""
        package_info = {}
        
        with tempfile.TemporaryDirectory() as tmpdir:
            for iso_name, iso_path in [('iso1', self.iso1), ('iso2', self.iso2)]:
                mount_point = Path(tmpdir) / iso_name
                mount_point.mkdir()
                
                # Try to mount
                rc, _, _ = self.run_command(
                    ['sudo', 'mount', '-o', 'loop,ro', str(iso_path), str(mount_point)],
                    check=False
                )
                
                if rc == 0:
                    # Look for Haiku packages
                    packages = []
                    pkg_dir = mount_point / 'system' / 'packages'
                    if pkg_dir.exists():
                        for pkg_file in pkg_dir.glob('*.hpkg'):
                            packages.append({
                                'name': pkg_file.name,
                                'size': pkg_file.stat().st_size
                            })
                    
                    package_info[iso_name] = {
                        'package_count': len(packages),
                        'packages': sorted(packages, key=lambda x: x['name'])[:50]  # Limit for report
                    }
                    
                    # Unmount
                    self.run_command(['sudo', 'umount', str(mount_point)], check=False)
                else:
                    package_info[iso_name] = {'error': 'Could not mount ISO'}
        
        # Compare packages if both were successful
        if 'packages' in package_info.get('iso1', {}) and 'packages' in package_info.get('iso2', {}):
            pkgs1 = {p['name'] for p in package_info['iso1']['packages']}
            pkgs2 = {p['name'] for p in package_info['iso2']['packages']}
            
            package_info['comparison'] = {
                'packages_only_in_iso1': sorted(list(pkgs1 - pkgs2)),
                'packages_only_in_iso2': sorted(list(pkgs2 - pkgs1)),
                'common_packages': len(pkgs1 & pkgs2)
            }
        
        return package_info
    
    def analyze_entropy(self) -> Dict:
        """Calculate entropy to detect compression/encryption differences"""
        def calculate_entropy(data: bytes) -> float:
            if not data:
                return 0.0
            
            # Count byte frequencies
            freq = [0] * 256
            for byte in data:
                freq[byte] += 1
            
            # Calculate Shannon entropy
            import math
            entropy = 0.0
            data_len = len(data)
            for count in freq:
                if count > 0:
                    p = count / data_len
                    entropy -= p * math.log2(p)
            
            return entropy
        
        entropy_info = {}
        sample_size = 1024 * 1024  # 1MB sample
        
        for iso_name, iso_path in [('iso1', self.iso1), ('iso2', self.iso2)]:
            with open(iso_path, 'rb') as f:
                # Sample from beginning, middle, and end
                samples = []
                file_size = iso_path.stat().st_size
                
                # Beginning
                f.seek(0)
                samples.append(f.read(min(sample_size, file_size)))
                
                # Middle
                if file_size > sample_size * 2:
                    f.seek(file_size // 2)
                    samples.append(f.read(min(sample_size, file_size - f.tell())))
                
                # End
                if file_size > sample_size * 3:
                    f.seek(max(0, file_size - sample_size))
                    samples.append(f.read())
                
                entropies = [calculate_entropy(sample) for sample in samples]
                entropy_info[iso_name] = {
                    'avg_entropy': sum(entropies) / len(entropies),
                    'min_entropy': min(entropies),
                    'max_entropy': max(entropies),
                    'samples_analyzed': len(entropies)
                }
        
        return entropy_info
    
    def analyze_timestamps(self) -> Dict:
        """Extract and compare creation/modification timestamps"""
        timestamp_info = {}
        
        for iso_name, iso_path in [('iso1', self.iso1), ('iso2', self.iso2)]:
            stat = iso_path.stat()
            
            # Read ISO 9660 volume descriptor for creation time
            creation_time = None
            with open(iso_path, 'rb') as f:
                # Skip to volume descriptors (sector 16, 2048 bytes each)
                f.seek(16 * 2048)
                vd = f.read(2048)
                
                if len(vd) >= 883 and vd[0] == 1:  # Primary Volume Descriptor
                    # Creation time is at offset 813-829 (17 bytes)
                    creation_bytes = vd[813:830]
                    if creation_bytes[0:14] != b'0' * 14:
                        try:
                            creation_str = creation_bytes[:14].decode('ascii')
                            if creation_str != '00000000000000':
                                creation_time = creation_str
                        except:
                            pass
            
            timestamp_info[iso_name] = {
                'file_mtime': datetime.datetime.fromtimestamp(stat.st_mtime).isoformat(),
                'file_ctime': datetime.datetime.fromtimestamp(stat.st_ctime).isoformat(),
                'iso_creation_time': creation_time,
                'age_days': (time.time() - stat.st_mtime) / (24 * 3600)
            }
        
        # Calculate time differences
        if 'iso1' in timestamp_info and 'iso2' in timestamp_info:
            mtime1 = timestamp_info['iso1']['file_mtime']
            mtime2 = timestamp_info['iso2']['file_mtime']
            timestamp_info['time_diff_hours'] = abs(
                (datetime.datetime.fromisoformat(mtime2) - 
                 datetime.datetime.fromisoformat(mtime1)).total_seconds() / 3600
            )
        
        return timestamp_info
    
    def analyze_filesystem_features(self) -> Dict:
        """Analyze filesystem features and extensions"""
        fs_info = {}
        
        for iso_name, iso_path in [('iso1', self.iso1), ('iso2', self.iso2)]:
            features = {
                'rock_ridge': False,
                'joliet': False,
                'udf': False,
                'el_torito': False,
                'apple_extensions': False
            }
            
            with open(iso_path, 'rb') as f:
                # Check first few sectors for various extensions
                for sector in range(16, 32):
                    f.seek(sector * 2048)
                    sector_data = f.read(2048)
                    
                    if b'CD001' in sector_data:
                        # Check for Joliet (UCS-2 encoding marker)
                        if sector_data[88:90] == b'%/@' or sector_data[88:90] == b'%/C' or sector_data[88:90] == b'%/E':
                            features['joliet'] = True
                    
                    # Check for Rock Ridge
                    if b'RRIP' in sector_data or b'IEEE_P1282' in sector_data:
                        features['rock_ridge'] = True
                    
                    # Check for El Torito boot
                    if b'EL TORITO SPECIFICATION' in sector_data:
                        features['el_torito'] = True
                    
                    # Check for UDF
                    if b'NSR02' in sector_data or b'NSR03' in sector_data:
                        features['udf'] = True
                    
                    # Check for Apple extensions
                    if b'Apple_partition_map' in sector_data or b'Apple_HFS' in sector_data:
                        features['apple_extensions'] = True
            
            fs_info[iso_name] = features
        
        return fs_info
    
    def analyze_compression_ratio(self) -> Dict:
        """Estimate compression efficiency by analyzing data patterns"""
        compression_info = {}
        
        for iso_name, iso_path in [('iso1', self.iso1), ('iso2', self.iso2)]:
            with open(iso_path, 'rb') as f:
                # Sample data for compression analysis
                sample_size = 1024 * 1024  # 1MB
                f.seek(0)
                sample = f.read(sample_size)
                
                # Count unique bytes
                unique_bytes = len(set(sample))
                
                # Count runs of identical bytes
                runs = 0
                if sample:
                    current_byte = sample[0]
                    run_length = 1
                    for byte in sample[1:]:
                        if byte == current_byte:
                            run_length += 1
                        else:
                            runs += 1
                            current_byte = byte
                            run_length = 1
                
                # Estimate compressibility
                compressibility = 1.0 - (unique_bytes / 256.0)
                redundancy = runs / len(sample) if sample else 0
                
                compression_info[iso_name] = {
                    'unique_bytes': unique_bytes,
                    'byte_variety': unique_bytes / 256.0,
                    'estimated_compressibility': compressibility,
                    'redundancy_ratio': redundancy,
                    'sample_size': len(sample)
                }
        
        return compression_info
    
    def analyze_binary_diff(self) -> Dict:
        """Perform binary diff analysis to find changed regions"""
        diff_info = {
            'total_differences': 0,
            'changed_sectors': [],
            'similarity_percent': 0.0,
            'largest_diff_offset': 0,
            'diff_distribution': {}
        }
        
        try:
            chunk_size = 2048  # ISO sector size
            changed_sectors = []
            total_chunks = 0
            different_chunks = 0
            
            with open(self.iso1, 'rb') as f1, open(self.iso2, 'rb') as f2:
                offset = 0
                while True:
                    chunk1 = f1.read(chunk_size)
                    chunk2 = f2.read(chunk_size)
                    
                    if not chunk1 and not chunk2:
                        break
                    
                    # Pad shorter chunk with zeros
                    if len(chunk1) != len(chunk2):
                        max_len = max(len(chunk1), len(chunk2))
                        chunk1 = chunk1.ljust(max_len, b'\x00')
                        chunk2 = chunk2.ljust(max_len, b'\x00')
                    
                    if chunk1 != chunk2:
                        different_chunks += 1
                        sector_num = offset // chunk_size
                        changed_sectors.append(sector_num)
                        
                        # Count byte differences in this chunk
                        byte_diffs = sum(1 for a, b in zip(chunk1, chunk2) if a != b)
                        diff_info['total_differences'] += byte_diffs
                        
                        if byte_diffs > 0:
                            diff_info['largest_diff_offset'] = offset
                    
                    total_chunks += 1
                    offset += chunk_size
                    
                    # Limit analysis to prevent excessive runtime
                    if total_chunks > 10000:  # ~20MB analysis
                        break
            
            if total_chunks > 0:
                diff_info['similarity_percent'] = (
                    (total_chunks - different_chunks) / total_chunks * 100
                )
            
            diff_info['changed_sectors'] = changed_sectors[:100]  # Limit for report
            diff_info['total_sectors_analyzed'] = total_chunks
            diff_info['different_sectors'] = different_chunks
            
        except Exception as e:
            diff_info['error'] = str(e)
        
        return diff_info
    
    def analyze_performance_metrics(self) -> Dict:
        """Analyze performance-related metrics"""
        perf_info = {}
        
        for iso_name, iso_path in [('iso1', self.iso1), ('iso2', self.iso2)]:
            # Read speed test
            start_time = time.time()
            with open(iso_path, 'rb') as f:
                # Read first 10MB to test read speed
                data = f.read(10 * 1024 * 1024)
            read_time = time.time() - start_time
            
            # Calculate metrics
            read_speed_mbps = (len(data) / (1024 * 1024)) / read_time if read_time > 0 else 0
            
            perf_info[iso_name] = {
                'read_speed_mbps': read_speed_mbps,
                'read_time_seconds': read_time,
                'test_data_size_mb': len(data) / (1024 * 1024)
            }
        
        return perf_info
    
    def generate_report(self, output_file: Optional[str] = None):
        """Generate comparison report"""
        print("ISO Comparison Tool - Starting analysis...")
        
        # Run all comparisons
        print("1. Comparing file sizes...")
        self.report['comparisons']['file_size'] = self.get_file_size()
        
        print("2. Calculating checksums...")
        self.report['comparisons']['checksums'] = self.get_checksums()
        
        print("3. Extracting ISO metadata...")
        self.report['comparisons']['iso_info'] = self.get_iso_info()
        
        print("4. Analyzing boot sectors...")
        self.report['comparisons']['boot_sector'] = self.analyze_boot_sector()
        
        print("5. Comparing file structure (may require sudo)...")
        self.report['comparisons']['file_structure'] = self.compare_file_structure()
        
        print("6. Analyzing Haiku packages (may require sudo)...")
        self.report['comparisons']['packages'] = self.analyze_haiku_packages()
        
        print("7. Calculating entropy and randomness...")
        self.report['comparisons']['entropy'] = self.analyze_entropy()
        
        print("8. Analyzing timestamps and creation times...")
        self.report['comparisons']['timestamps'] = self.analyze_timestamps()
        
        print("9. Detecting filesystem features...")
        self.report['comparisons']['filesystem_features'] = self.analyze_filesystem_features()
        
        print("10. Estimating compression characteristics...")
        self.report['comparisons']['compression'] = self.analyze_compression_ratio()
        
        print("11. Performing binary diff analysis...")
        self.report['comparisons']['binary_diff'] = self.analyze_binary_diff()
        
        print("12. Testing read performance...")
        self.report['comparisons']['performance'] = self.analyze_performance_metrics()
        
        # Generate text report
        self.print_report()
        
        # Save JSON report if requested
        if output_file:
            with open(output_file, 'w') as f:
                json.dump(self.report, f, indent=2, default=str)
            print(f"\nDetailed JSON report saved to: {output_file}")
    
    def print_report(self):
        """Print human-readable report"""
        print("\n" + "="*80)
        print("ISO COMPARISON REPORT")
        print("="*80)
        print(f"ISO 1: {self.iso1.name}")
        print(f"ISO 2: {self.iso2.name}")
        print(f"Date: {self.report['timestamp']}")
        
        # File size comparison
        size_info = self.report['comparisons']['file_size']
        print(f"\n📏 FILE SIZE COMPARISON:")
        print(f"  ISO 1: {size_info['iso1_size'] / (1024**2):.2f} MB")
        print(f"  ISO 2: {size_info['iso2_size'] / (1024**2):.2f} MB")
        print(f"  Difference: {size_info['size_diff_mb']:.2f} MB ({size_info['size_diff_percent']:.1f}%)")
        
        # Checksums
        checksums = self.report['comparisons']['checksums']
        print(f"\n🔐 CHECKSUMS:")
        print(f"  ISOs are {'IDENTICAL' if checksums['identical'] else 'DIFFERENT'}")
        if not checksums['identical']:
            print(f"  ISO 1 SHA256: {checksums['iso1_sha256'][:16]}...")
            print(f"  ISO 2 SHA256: {checksums['iso2_sha256'][:16]}...")
        
        # Boot sector
        boot_info = self.report['comparisons']['boot_sector']
        print(f"\n💾 BOOT SECTOR:")
        print(f"  Boot sectors {'match' if boot_info['boot_sectors_identical'] else 'differ'}")
        print(f"  ISO 1 signatures: {', '.join(boot_info['iso1']['signatures']) or 'none detected'}")
        print(f"  ISO 2 signatures: {', '.join(boot_info['iso2']['signatures']) or 'none detected'}")
        
        # File structure
        file_struct = self.report['comparisons']['file_structure']
        if 'error' not in file_struct:
            print(f"\n📁 FILE STRUCTURE:")
            print(f"  ISO 1 files: {file_struct['iso1_file_count']}")
            print(f"  ISO 2 files: {file_struct['iso2_file_count']}")
            print(f"  Files only in ISO 1: {len(file_struct['files_only_in_iso1'])}")
            print(f"  Files only in ISO 2: {len(file_struct['files_only_in_iso2'])}")
            print(f"  Common files: {file_struct['common_files']}")
        
        # Packages
        pkg_info = self.report['comparisons']['packages']
        if 'iso1' in pkg_info and 'error' not in pkg_info['iso1']:
            print(f"\n📦 HAIKU PACKAGES:")
            print(f"  ISO 1 packages: {pkg_info['iso1']['package_count']}")
            print(f"  ISO 2 packages: {pkg_info['iso2']['package_count']}")
            if 'comparison' in pkg_info:
                print(f"  Packages only in ISO 1: {len(pkg_info['comparison']['packages_only_in_iso1'])}")
                print(f"  Packages only in ISO 2: {len(pkg_info['comparison']['packages_only_in_iso2'])}")
        
        # Entropy analysis
        entropy_info = self.report['comparisons']['entropy']
        if 'iso1' in entropy_info and 'iso2' in entropy_info:
            print(f"\n🔀 ENTROPY & RANDOMNESS:")
            print(f"  ISO 1 avg entropy: {entropy_info['iso1']['avg_entropy']:.3f}")
            print(f"  ISO 2 avg entropy: {entropy_info['iso2']['avg_entropy']:.3f}")
            entropy_diff = abs(entropy_info['iso1']['avg_entropy'] - entropy_info['iso2']['avg_entropy'])
            print(f"  Entropy difference: {entropy_diff:.3f}")
        
        # Timestamps
        timestamp_info = self.report['comparisons']['timestamps']
        if 'time_diff_hours' in timestamp_info:
            print(f"\n🕐 TIMESTAMPS:")
            print(f"  Time difference: {timestamp_info['time_diff_hours']:.1f} hours")
            print(f"  ISO 1 age: {timestamp_info['iso1']['age_days']:.1f} days")
            print(f"  ISO 2 age: {timestamp_info['iso2']['age_days']:.1f} days")
        
        # Filesystem features
        fs_features = self.report['comparisons']['filesystem_features']
        if 'iso1' in fs_features:
            print(f"\n🗂️  FILESYSTEM FEATURES:")
            features1 = [k for k, v in fs_features['iso1'].items() if v]
            features2 = [k for k, v in fs_features['iso2'].items() if v]
            print(f"  ISO 1 features: {', '.join(features1) or 'none'}")
            print(f"  ISO 2 features: {', '.join(features2) or 'none'}")
            common_features = set(features1) & set(features2)
            if common_features:
                print(f"  Common features: {', '.join(common_features)}")
        
        # Binary similarity
        binary_diff = self.report['comparisons']['binary_diff']
        if 'similarity_percent' in binary_diff:
            print(f"\n🔍 BINARY ANALYSIS:")
            print(f"  Similarity: {binary_diff['similarity_percent']:.2f}%")
            print(f"  Different sectors: {binary_diff['different_sectors']}")
            print(f"  Total differences: {binary_diff['total_differences']:,} bytes")
        
        # Performance
        perf_info = self.report['comparisons']['performance']
        if 'iso1' in perf_info and 'iso2' in perf_info:
            print(f"\n⚡ PERFORMANCE:")
            print(f"  ISO 1 read speed: {perf_info['iso1']['read_speed_mbps']:.1f} MB/s")
            print(f"  ISO 2 read speed: {perf_info['iso2']['read_speed_mbps']:.1f} MB/s")
        
        # Compression estimates
        compression_info = self.report['comparisons']['compression']
        if 'iso1' in compression_info and 'iso2' in compression_info:
            print(f"\n🗜️  COMPRESSION ANALYSIS:")
            print(f"  ISO 1 compressibility: {compression_info['iso1']['estimated_compressibility']:.2%}")
            print(f"  ISO 2 compressibility: {compression_info['iso2']['estimated_compressibility']:.2%}")
            print(f"  ISO 1 byte variety: {compression_info['iso1']['byte_variety']:.2%}")
            print(f"  ISO 2 byte variety: {compression_info['iso2']['byte_variety']:.2%}")
        
        print("\n" + "="*80)

def main():
    parser = argparse.ArgumentParser(description='Compare two ISO images')
    parser.add_argument('iso1', help='Path to first ISO image')
    parser.add_argument('iso2', help='Path to second ISO image')
    parser.add_argument('-o', '--output', help='Output JSON report file')
    parser.add_argument('--no-mount', action='store_true', 
                       help='Skip operations requiring mount (no sudo needed)')
    
    args = parser.parse_args()
    
    # Validate inputs
    for iso in [args.iso1, args.iso2]:
        if not Path(iso).exists():
            print(f"Error: {iso} does not exist")
            sys.exit(1)
    
    # Run comparison
    comparator = ISOComparator(args.iso1, args.iso2)
    comparator.generate_report(args.output)

if __name__ == '__main__':
    main()