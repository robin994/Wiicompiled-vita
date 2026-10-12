#!/usr/bin/env python3
"""Small-fixture contract for the Vita source provenance gate."""
import tempfile
from pathlib import Path
import unittest

from record_vita_delivery import source_digest


class DeliverySourceDigestTests(unittest.TestCase):
    def test_detects_dirty_sources_and_new_untracked_headers(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            (root/"CMakeLists.txt").write_text("project(sample)\n")
            (root/"build-vita.sh").write_text("#!/bin/sh\n")
            (root/"runtime"/"include").mkdir(parents=True)
            header=root/"runtime"/"include"/"version.hpp"
            header.write_text("constexpr int value=1;\n")
            original,count=source_digest(root)
            self.assertEqual(source_digest(root),(original,count))
            header.write_text("constexpr int value=2;\n")
            dirty,dirty_count=source_digest(root)
            self.assertNotEqual(original,dirty)
            self.assertEqual(count,dirty_count)
            new=root/"aurora-main"/"platforms"/"vita"/"new_feature.hpp"
            new.parent.mkdir(parents=True)
            new.write_text("#pragma once\n")
            newer,newer_count=source_digest(root)
            self.assertNotEqual(dirty,newer)
            self.assertEqual(newer_count,count+1)

    def test_ignores_transient_build_and_git_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            (root/"CMakeLists.txt").write_text("project(sample)\n")
            (root/"build-vita.sh").write_text("#!/bin/sh\n")
            base=source_digest(root)
            for folder in ("runtime/build", "aurora-main/.git", "generated/obj"):
                directory=root/folder
                directory.mkdir(parents=True)
                (directory/"temporary.cpp").write_text("int transient;\n")
            self.assertEqual(source_digest(root),base)


if __name__=="__main__":
    unittest.main()
