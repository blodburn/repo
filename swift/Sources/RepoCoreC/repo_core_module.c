/*
 * Translation unit for the RepoCoreC target.
 *
 * The target exists to publish repo_core.h to Swift; the symbols themselves come
 * from the repo-core static library that Package.swift links. SwiftPM requires a
 * source file in a target, so this file only pulls the header in.
 */
#include "repo_core.h"
