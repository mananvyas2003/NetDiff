# NetDiff KiCad Action plugin — import registers the toolbar action when pcbnew is present.
from .action import register, run_review

register()

__all__ = ["register", "run_review"]
