#!/bin/bash

# An empty $'' keeps one field.

f() { echo "fields=$#"; }
f $''
