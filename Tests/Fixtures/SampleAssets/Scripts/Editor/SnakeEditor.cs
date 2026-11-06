using System;
using Ursa.Editor;
using Ursa.Demo;

[CustomEditor(typeof(SnakeController))]
public sealed class SnakeControllerInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		SerializedObject.Update();
		EditorGui.Text("棋盘、蛇身和食物均保存在场景中；脚本只更新已有对象。");
		EditorGui.PropertyField(SerializedObject.FindProperty("width"), "棋盘宽度");
		EditorGui.PropertyField(SerializedObject.FindProperty("height"), "棋盘高度");
		EditorGui.PropertyField(SerializedObject.FindProperty("seed"), "随机种子");
		EditorGui.PropertyField(SerializedObject.FindProperty("segmentObjects"), "场景蛇身对象");
		EditorGui.PropertyField(SerializedObject.FindProperty("foodObject"), "场景食物对象");
		SerializedObject.ApplyModifiedProperties();
	}
}

public static class SnakeEditorMenus
{
	[MenuItem("Tools/Snake/Save Current Scene")]
	private static void SaveCurrentScene()
	{
		if (EditorContext.Document != null)
		{
			EditorContext.Save();
		}
	}
}

[EditorWindowMenu("Tools/Snake/Controls")]
public sealed class SnakeControlsWindow : EditorWindow
{
	public override string Title => "Snake Controls";

	public override void OnGUI()
	{
		EditorGui.Text("方向键或 WASD：转向");
		EditorGui.Text("P：暂停 / 继续");
		EditorGui.Text("空格：重新开始");
	}
}
