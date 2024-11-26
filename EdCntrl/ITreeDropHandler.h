#pragma once

class ITreeDropHandler
{
  public:
	virtual ~ITreeDropHandler() = default;

	virtual bool IsAllowedToStartDrag() = 0;
	virtual bool IsAllowedToDrop(HTREEITEM target, bool afterTarget) = 0;
	virtual void CopyDroppedItem(HTREEITEM target, bool afterTarget) = 0;
	virtual void MoveDroppedItem(HTREEITEM target, bool afterTarget) = 0;
	virtual void OnTripleClick() = 0;
};
