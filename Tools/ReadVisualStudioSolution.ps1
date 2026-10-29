function Get-UrsaSolutionPath([string]$BuildRoot) {
    foreach ($name in @('UrsaEngine.slnx','UrsaEngine.sln')) {
        $candidate = Join-Path $BuildRoot $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "No generated UrsaEngine solution at $BuildRoot"
}
function Get-UrsaSolutionProjects([string]$BuildRoot) {
    $path = Get-UrsaSolutionPath $BuildRoot
    $solution = [IO.File]::ReadAllText($path)
    $projects = @{}
    if ([IO.Path]::GetExtension($path) -eq '.slnx') {
        [xml]$xml = $solution
        foreach ($node in $xml.SelectNodes('//Project[@Path]')) {
            $projectPath = $node.GetAttribute('Path')
            if (-not [IO.Path]::IsPathRooted($projectPath)) { $projectPath = Join-Path $BuildRoot $projectPath }
            $folder = if ($node.ParentNode.LocalName -eq 'Folder') { $node.ParentNode.GetAttribute('Name').Trim('/') } else { '' }
            $name = [IO.Path]::GetFileNameWithoutExtension($projectPath)
            $projects[$name] = @{ Path = $projectPath; Folder = $folder; Node = $node }
        }
    } else {
        $entries = @{}
        $parents = @{}
        foreach ($match in [regex]::Matches($solution, 'Project\("\{([^}]+)\}"\) = "([^"]+)", "([^"]+)", "\{([^}]+)\}"')) {
            $entries[$match.Groups[4].Value] = @{ Type=$match.Groups[1].Value; Name=$match.Groups[2].Value; Path=$match.Groups[3].Value }
        }
        $nested = [regex]::Match($solution, '(?s)GlobalSection\(NestedProjects\).*?EndGlobalSection').Value
        foreach ($match in [regex]::Matches($nested, '\{([^}]+)\} = \{([^}]+)\}')) { $parents[$match.Groups[1].Value]=$match.Groups[2].Value }
        foreach ($id in @($entries.Keys)) {
            $entry = $entries[$id]
            if ($entry.Type -eq '2150E333-8FDC-42A3-9474-1A3956D46DE8') { continue }
            $parts = @()
            $cursor = $id
            while ($parents.ContainsKey($cursor)) { $cursor=$parents[$cursor]; $parts=@($entries[$cursor].Name)+$parts }
            $projectPath = $entry.Path
            if (-not [IO.Path]::IsPathRooted($projectPath)) { $projectPath = Join-Path $BuildRoot $projectPath }
            $projects[$entry.Name] = @{ Path=$projectPath; Folder=$parts -join '/' }
        }
    }
    return $projects
}
