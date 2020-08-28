#include <stdexcept>

#include "ArrayVar.h"

ArrayVarElementContainer::ArrayVarElementContainer(bool isArray)
{
	this->isArray_ = isArray;
	if (isArray)
	{
		this->array_ = std::make_unique<std::vector<ArrayElement>>();
	}
	else
	{
		this->map_ = std::make_unique<std::map<ArrayKey, ArrayElement>>();
	}
}

ArrayVarElementContainer::ArrayVarElementContainer() = default;

//ArrayElement& ArrayVarElementContainer::operator[](const ArrayKey& i) const
//{
//	if (isArray_) [[likely]]
//	{
//		const std::size_t index = i.key.num;
//		if (index >= array_->size())
//		{
//			array_->push_back(ArrayElement());
//			return array_->back();
//		}
//		return (*array_)[index];
//	}
//	return (*map_)[i];
//}

ArrayVarElementContainer::iterator::iterator() = default;

ArrayVarElementContainer::iterator::iterator(const std::map<ArrayKey, ArrayElement>::iterator iter)
{
	this->isArray_ = false;
	this->mapIter_ = iter;
}

ArrayVarElementContainer::iterator::iterator(std::vector<ArrayElement>::iterator iter, ArrayVarElementContainer const* containerPtr)
{
	this->isArray_ = true;
	this->arrIter_ = iter;
	this->containerPtr_ = containerPtr;
}

void ArrayVarElementContainer::iterator::operator++()
{
	if (isArray_) [[likely]]
	{
		++arrIter_;
	}
	else
	{
		++mapIter_;
	}
}

void ArrayVarElementContainer::iterator::operator--()
{
	if (isArray_) [[likely]]
	{
		--arrIter_;
	}
	else
	{
		--mapIter_;
	}
}

bool ArrayVarElementContainer::iterator::operator!=(const iterator& other) const
{
	if (other.isArray_ != this->isArray_)
	{
		return true;
	}
	if (isArray_) [[likely]]
	{
		return other.arrIter_ != this->arrIter_;
	}
	return other.mapIter_ != this->mapIter_;
}

static ArrayKey s_packedKey(0.0);

const ArrayKey* ArrayVarElementContainer::iterator::first() const
{
	if (isArray_) [[likely]]
	{
		s_packedKey.key.num = arrIter_ - containerPtr_->getVectorRef().begin();
		return &s_packedKey;
	}
	return &mapIter_->first;
}

ArrayElement* ArrayVarElementContainer::iterator::second() const
{
	if (isArray_) [[likely]]
	{
		return arrIter_._Ptr;
	}
	return &mapIter_->second;
}

ArrayVarElementContainer::iterator ArrayVarElementContainer::find(const ArrayKey& key) const
{
	if (isArray_) [[likely]]
	{
		std::size_t index = key.key.num;
		if (index >= array_->size())
		{
			return iterator(array_->end(), this);
		}
		return iterator(array_->begin() + key.key.num, this);
	}
	return iterator(map_->find(key));
}

std::size_t ArrayVarElementContainer::count(const ArrayKey* key) const
{
	if (isArray_) [[likely]]
	{
		std::size_t index = key->key.num;
		return (index < array_->size()) ? 1 : 0;
	}
	return map_->count(*key);
}

ArrayVarElementContainer::iterator ArrayVarElementContainer::begin() const
{
	if (isArray_) [[likely]]
	{
		return iterator(array_->begin(), this);
	}
	return iterator(map_->begin());
}

ArrayVarElementContainer::iterator ArrayVarElementContainer::end() const
{
	if (isArray_) [[likely]]
	{
		return iterator(array_->end(), this);
	}
	return iterator(map_->end());
}

std::size_t ArrayVarElementContainer::size() const
{
	if (isArray_) [[likely]]
	{
		return array_->size();
	}
	return map_->size();
}

std::size_t ArrayVarElementContainer::erase(const ArrayKey* key) const
{
	if (isArray_) [[likely]]
	{
		UInt32 idx = (int)key->key.num;
		if (idx >= array_->size())
			return 0;
		(*array_)[idx].Unset();
		array_->erase(array_->begin() + idx);
	}
	else
	{
		auto findKey = map_->find(*key);
		if (findKey == map_->end())
			return 0;
		findKey->second.Unset();
		map_->erase(findKey);
	}
	return 1;
}

std::size_t ArrayVarElementContainer::erase(const ArrayKey* low, const ArrayKey* high) const
{
	if (!isArray_)
		throw std::invalid_argument("Attempt to erase range of elements from map");
	UInt32 iLow = (int)low->key.num, iHigh = (int)high->key.num;
	if ((iHigh > array_->size()) || (iLow > array_->size()))
		throw std::out_of_range("Attempt to erase out of range array var");
	for (UInt32 idx = iLow; idx < iHigh; idx++)
		(*array_)[idx].Unset();
	array_->erase(array_->begin() + iLow, array_->begin() + iHigh);
	return iHigh - iLow;
}

void ArrayVarElementContainer::clear() const
{
	if (isArray_) [[likely]]
	{
		for (UInt32 idx = 0; idx < array_->size(); idx++)
			(*array_)[idx].Unset();
		array_->clear();
	}
	else
	{
		for (auto iter = map_->begin(); iter != map_->end(); ++iter)
			iter->second.Unset();
		map_->clear();
	}
}

std::vector<ArrayElement>& ArrayVarElementContainer::getVectorRef() const
{
	return *array_;
}

std::map<ArrayKey, ArrayElement>& ArrayVarElementContainer::getMapRef() const
{
	return *map_;
}