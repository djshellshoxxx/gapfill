# WP4 facade, drawer and footer (docs/architecture/MODULES.md, "WP4 recipe features").
list(APPEND GAPFILL_SOURCES
    Source/features/recipes/RecipeDrawer.cpp
    Source/features/recipes/RecipeFooter.cpp
    Source/features/recipes/Help.cpp
)
list(APPEND GAPFILL_SOURCES
    Source/features/export/ExportService.cpp
    Source/features/export/WavExport.cpp
)
list(APPEND GAPFILL_SOURCES
    Source/features/abcompare/MatchLevels.cpp
    Source/features/abcompare/RecipeAB.cpp
)
