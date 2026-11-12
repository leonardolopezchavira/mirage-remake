#!/usr/bin/env python3
"""Renders the Mirage wordmark used on the editor panel: resources/ui/wordmark.png
(yellow on transparent, cap height 160 px; the editor scales it down)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wordmark
out = sys.argv[1]
wordmark.render("Mirage", 160, (244, 196, 12), pad=0.05).save(out)
print("wrote", out)
